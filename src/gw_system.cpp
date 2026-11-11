/*
 * ScummVM backend for the Game & Watch (Retro-Go SD core) -- and for the PC test build.
 *
 * OSystem on top of platform.h: an 8-bit screen handed to the LCD, the buttons as a mouse,
 * files on the SD card (or, read-only, mapped from external flash), the default timer and
 * event managers driven from pollEvent()/delayMillis(), no threads. Sound is mixed in the
 * same way: the platform calls back (plat_set_pump) whenever it waits or shows a frame, and
 * the mixer tops its buffer up.
 */
#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "common/scummsys.h"
#include "common/events.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/str.h"
#include "common/textconsole.h"
#include "graphics/surface.h"
#include "backends/modular-backend.h"
#include "backends/mutex/null/null-mutex.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"
#include "backends/events/default/default-events.h"
#include "backends/mixer/mixer.h"
#include "audio/mixer_intern.h"
#include "backends/graphics/null/null-graphics.h"
#include "backends/fs/abstract-fs.h"
#include "backends/fs/fs-factory.h"
#include "base/main.h"
#include "engines/engine.h"
#include "common/savefile.h"
#include "common/config-manager.h"
#include "platform.h"
#include "osk.h"
#include "pack.h"

/* ================================================================ files */

/* A file in external flash (on the PC: in memory). Reads give the file's original bytes even
   when the copy is stored decrypted (key != 0, see plat_map_file), so detection's MD5s and the
   engine's own XOR see what they expect; gwDirectResource() hands out the stored bytes. */
class GWMappedStream : public Common::SeekableReadStream {
public:
	const byte *_data;
	uint32 _size, _pos = 0;
	byte _key;
	bool _eos = false;
	GWMappedStream *_next;
	static GWMappedStream *_open;                     // every live one, to recognise them

	GWMappedStream(const byte *data, uint32 size, byte key) : _data(data), _size(size), _key(key) {
		_next = _open; _open = this;
	}
	~GWMappedStream() override {
		for (GWMappedStream **p = &_open; *p; p = &(*p)->_next)
			if (*p == this) { *p = _next; break; }
	}
	bool eos() const override { return _eos; }
	void clearErr() override { _eos = false; }
	int64 pos() const override { return _pos; }
	int64 size() const override { return _size; }
	bool seek(int64 offs, int whence = SEEK_SET) override {
		int64 p = whence == SEEK_SET ? offs : whence == SEEK_CUR ? _pos + offs : _size + offs;
		if (p < 0 || p > _size) return false;
		_pos = (uint32)p; _eos = false;
		return true;
	}
	uint32 read(void *buf, uint32 n) override {
		if (n > _size - _pos) { n = _size - _pos; _eos = true; }
		memcpy(buf, _data + _pos, n);
		if (_key) for (byte *b = (byte *)buf, *e = b + n; b < e; b++) *b ^= _key;
		_pos += n;
		return n;
	}
};
GWMappedStream *GWMappedStream::_open;

/* A file on the card being read, without holding one of the firmware's file handles for as long
   as the stream lives: the firmware has six for everything, and Full Throttle keeps more
   streams than that alive at once (its data file twice -- resources and streamed sounds --
   the speech file, the video, a font, the log...). On the console the seventh fopen() failed:
   videos "not found", and a video read through the wrong stream. Here at most GW_CARD_OPEN
   are really open; the others are reopened (and seeked) when they are next read. */
#define GW_CARD_OPEN 3
class GWCardFile {
	Common::String _path;
	static struct Slot { GWCardFile *owner; void *f; uint32 pos; uint32 stamp; } _slots[GW_CARD_OPEN];
	static uint32 _clock;
	Slot *slot() {
		Slot *lru = &_slots[0];
		for (auto &s : _slots) {
			if (s.owner == this) { s.stamp = ++_clock; return &s; }
			if (!s.owner) lru = &s;
			else if (lru->owner && s.stamp < lru->stamp) lru = &s;
		}
		if (lru->owner) plat_fclose(lru->f);
		lru->owner = nullptr;
		lru->f = plat_fopen(_path.c_str(), "rb");
		if (!lru->f) return nullptr;
		lru->owner = this;
		lru->pos = 0;
		lru->stamp = ++_clock;
		return lru;
	}
public:
	GWCardFile(const Common::String &path) : _path(path) {}
	~GWCardFile() {
		for (auto &s : _slots)
			if (s.owner == this) { plat_fclose(s.f); s.owner = nullptr; s.f = nullptr; }
	}
	uint32 readAt(uint32 pos, void *buf, uint32 n) {
		Slot *s = slot();
		if (!s) return 0;
		if (s->pos != pos && !plat_fseek(s->f, (int32_t)pos, SEEK_SET)) { s->pos = 0xffffffff; return 0; }
		uint32 got = plat_fread(buf, n, s->f);
		s->pos = pos + got;
		return got;
	}
};
GWCardFile::Slot GWCardFile::_slots[GW_CARD_OPEN];
uint32 GWCardFile::_clock;

/* A plain file on the card, read only */
class GWCardStream : public Common::SeekableReadStream {
	GWCardFile _file;
	uint32 _size, _pos = 0;
	bool _eos = false;
public:
	GWCardStream(const Common::String &path, uint32 size) : _file(path), _size(size) {}
	bool eos() const override { return _eos; }
	void clearErr() override { _eos = false; }
	int64 pos() const override { return _pos; }
	int64 size() const override { return _size; }
	bool seek(int64 offs, int whence = SEEK_SET) override {
		int64 p = whence == SEEK_SET ? offs : whence == SEEK_CUR ? _pos + offs : _size + offs;
		if (p < 0 || p > _size) return false;
		_pos = (uint32)p; _eos = false;
		return true;
	}
	uint32 read(void *buf, uint32 n) override {
		if (n > _size - _pos) { n = _size - _pos; _eos = true; }
		uint32 got = n ? _file.readAt(_pos, buf, n) : 0;
		_pos += got;
		if (got < n) _eos = true;
		return got;
	}
};

/* A data file read from the card that has a resource pack in flash (src/pack.c: FT.LA1):
   reads that fall inside a packed block come from the flash, the rest from the card. */
class GWPackedStream : public Common::SeekableReadStream {
public:
	GWCardFile _file;
	uint32 _size, _pos = 0;
	const pack_head_t *_head;
	bool _eos = false;
	GWPackedStream *_next;
	static GWPackedStream *_open;

	GWPackedStream(const Common::String &path, uint32 size, const pack_head_t *head) : _file(path), _size(size), _head(head) {
		_next = _open; _open = this;
	}
	~GWPackedStream() override {
		for (GWPackedStream **p = &_open; *p; p = &(*p)->_next)
			if (*p == this) { *p = _next; break; }
	}
	bool eos() const override { return _eos; }
	void clearErr() override { _eos = false; }
	int64 pos() const override { return _pos; }
	int64 size() const override { return _size; }
	bool seek(int64 offs, int whence = SEEK_SET) override {
		int64 p = whence == SEEK_SET ? offs : whence == SEEK_CUR ? _pos + offs : _size + offs;
		if (p < 0 || p > _size) return false;
		_pos = (uint32)p; _eos = false;
		return true;
	}
	const byte *inFlash(uint32 n) const {
		const pack_entry_t *e = svm_pack_find(_head, _pos, n);
		return e ? (const byte *)_head + e->pack + (_pos - e->src) : nullptr;
	}
	uint32 read(void *buf, uint32 n) override {
		if (n > _size - _pos) { n = _size - _pos; _eos = true; }
		if (const byte *p = inFlash(n)) {
			memcpy(buf, p, n);
			_pos += n;
			return n;
		}
		uint32 got = n ? _file.readAt(_pos, buf, n) : 0;
		_pos += got;
		if (got < n) _eos = true;
		return got;
	}
};
GWPackedStream *GWPackedStream::_open;

/* patches/scummvm-slim.patch: the next n bytes of 'stream' where they lie, as the engine
   would read them with its XOR byte 'enc' -- or NULL (not in flash, or stored differently).
   The stream moves past them. */
const byte *gwDirectResource(Common::SeekableReadStream *stream, uint32 n, byte enc)
{
	for (GWMappedStream *m = GWMappedStream::_open; m; m = m->_next)
		if (m == stream) {
			if (m->_key != enc || n > m->_size - m->_pos) return nullptr;
			const byte *p = m->_data + m->_pos;
			m->_pos += n;
			return p;
		}
	for (GWPackedStream *m = GWPackedStream::_open; m; m = m->_next)
		if (m == stream) {
			const byte *p = enc || n > m->_size - m->_pos ? nullptr : m->inFlash(n);
			if (p) m->_pos += n;
			return p;
		}
	return nullptr;
}

/* patches/scummvm-slim.patch (Sky::Disk::loadFile): file 'nr' of Beneath a Steel Sky's sky.dsk
   as the engine would load it, where it lies in the game's pack in the flash (src/pack.c), or
   NULL: not in the pack (speech), or no pack */
const byte *gwSkyFile(uint16 nr, uint32 *size)
{
	static const pack_head_t *head;
	static bool looked;
	if (!looked) {
		looked = true;
		for (const char *name : { "sky.dsk", "SKY.DSK" }) {
			Common::String path = Common::String(plat_game_path()) + "/" + name;
			head = (const pack_head_t *)plat_map_pack(path.c_str());
			if (head) break;
		}
		if (head && head->reserved != PACK_SKY) head = nullptr;
	}
	const pack_entry_t *e = head ? svm_pack_file(head, nr) : nullptr;
	if (!e) return nullptr;
	*size = e->size;
	return (const byte *)head + e->pack;
}

class GWFileStream : public Common::SeekableReadStream, public Common::SeekableWriteStream {
	void *_f;
	int64 _size;
	bool _eos = false, _err = false;
public:
	GWFileStream(void *f, int64 size) : _f(f), _size(size) {}
	~GWFileStream() override { plat_fclose(_f); }
	bool err() const override { return _err; }
	void clearErr() override { _err = false; _eos = false; }
	bool eos() const override { return _eos; }
	uint32 read(void *buf, uint32 n) override {
		uint32 got = plat_fread(buf, n, _f);
		if (got < n) _eos = true;
		return got;
	}
	uint32 write(const void *buf, uint32 n) override {
		uint32 put = plat_fwrite(buf, n, _f);
		if (put < n) _err = true;
		int64 p = plat_ftell(_f);
		if (p > _size) _size = p;
		return put;
	}
	bool flush() override { return true; }
	int64 pos() const override { return plat_ftell(_f); }
	int64 size() const override { return _size; }
	bool seek(int64 offs, int whence = SEEK_SET) override {
		_eos = false;
		return plat_fseek(_f, (int32)offs, whence);
	}
};

class GWFSNode : public AbstractFSNode {
	Common::String _path;                         // absolute, no trailing slash ("" = root)
	mutable bool _statted = false;
	mutable plat_stat_t _st;
	const plat_stat_t &st() const {
		if (!_statted) { _st = plat_stat(_path.empty() ? "/" : _path.c_str()); _statted = true; }
		return _st;
	}
public:
	explicit GWFSNode(const Common::String &path) : _path(path) {
		while (_path.size() > 1 && _path.lastChar() == '/') _path.deleteLastChar();
		if (_path == "/") _path.clear();
	}
	GWFSNode(const Common::String &path, bool isDir, uint32 size) : GWFSNode(path) {
		_st.exists = true; _st.is_dir = isDir; _st.size = size; _statted = true;
	}
	AbstractFSNode *getChild(const Common::String &n) const override { return new GWFSNode(_path + "/" + n); }
	AbstractFSNode *getParent() const override {
		const char *s = _path.c_str(), *slash = strrchr(s, '/');
		return new GWFSNode(slash ? Common::String(s, slash) : Common::String());
	}
	bool exists() const override { return _path.empty() || st().exists; }
	bool getChildren(AbstractFSList &list, ListMode mode, bool hidden) const override {
		struct Ctx { AbstractFSList *list; ListMode mode; const Common::String *dir; } c = { &list, mode, &_path };
		return plat_list_dir(_path.empty() ? "/" : _path.c_str(),
			[](const char *name, bool isDir, uint32_t size, void *arg) {
				Ctx *x = (Ctx *)arg;
				if ((x->mode == Common::FSNode::kListFilesOnly && isDir) ||
				    (x->mode == Common::FSNode::kListDirectoriesOnly && !isDir)) return;
				x->list->push_back(new GWFSNode(*x->dir + "/" + name, isDir, size));
			}, &c);
	}
	Common::U32String getDisplayName() const override { return Common::U32String(getName()); }
	Common::String getName() const override {
		const char *slash = strrchr(_path.c_str(), '/');
		return slash ? Common::String(slash + 1) : _path;
	}
	Common::String getPath() const override { return _path.empty() ? Common::String("/") : _path; }
	bool isDirectory() const override { return _path.empty() || st().is_dir; }
	bool isReadable() const override { return exists(); }
	bool isWritable() const override { return true; }
	Common::SeekableReadStream *createReadStream() override {
		uint32_t size = 0;
		uint8_t key = 0;
		const uint8_t *map = plat_map_file(_path.c_str(), &size, &key);
		if (map) return new GWMappedStream(map, size, key);
		if (!st().exists || st().is_dir) return nullptr;
		if (const void *pack = plat_map_pack(_path.c_str()))
			if (((const pack_head_t *)pack)->reserved != PACK_SKY)     // (that one is asked for by file: gwSkyFile)
				return new GWPackedStream(_path, st().size, (const pack_head_t *)pack);
		return new GWCardStream(_path, st().size);
	}
	Common::SeekableWriteStream *createWriteStream(bool atomic) override {
		void *f = plat_fopen(_path.c_str(), "wb");
		_statted = false;
		return f ? new GWFileStream(f, 0) : nullptr;
	}
	bool createDirectory() override { _statted = false; return plat_mkdir(_path.c_str()); }
};

/* ================================================================ saves */

/* ScummVM's own saves go to plat_save_path() under its usual names (<target>.sNN). Slot 99
   is the Retro-Go PAUSE/SET menu's: while gw_rg_save()/gw_rg_load() run, its file is the
   one the firmware chose (/saves/<system>/<game>/...), so the launcher's slots, previews
   and "resume" work as for any other core. */
#define RG_SLOT 99
static char g_rgPath[256];

class GWSaveFileManager : public DefaultSaveFileManager {
	static bool redirected(const Common::String &name) {
		// (SCUMM's <target>.s99, Beneath a Steel Sky's SKY-VM.099, the other engines' <name>.099)
		return g_rgPath[0] && (name.hasSuffix(Common::String::format(".s%02d", RG_SLOT)) ||
		                       name.hasSuffix(Common::String::format(".%03d", RG_SLOT)));
	}
public:
	GWSaveFileManager() : DefaultSaveFileManager(Common::Path(plat_save_path())) {}
	Common::InSaveFile *openForLoading(const Common::String &name) override {
		if (!redirected(name)) return DefaultSaveFileManager::openForLoading(name);
		plat_stat_t st = plat_stat(g_rgPath);
		void *f = st.exists ? plat_fopen(g_rgPath, "rb") : nullptr;
		return f ? new GWFileStream(f, st.size) : nullptr;
	}
	Common::OutSaveFile *openForSaving(const Common::String &name, bool compress) override {
		if (!redirected(name)) return DefaultSaveFileManager::openForSaving(name, compress);
		void *f = plat_fopen(g_rgPath, "wb");
		return f ? new Common::OutSaveFile(new GWFileStream(f, 0)) : nullptr;
	}
};

extern "C" int gw_scumm_save(int slot);       /* patches/scummvm-slim.patch (saveload.cpp) */
extern "C" int gw_scumm_load(int slot);
extern "C" int gw_sky_save(int slot);         /* patches/scummvm-slim.patch (sky.cpp) */
extern "C" int gw_sky_load(int slot);

/* which engine is running: the hooks above cast g_engine to their own */
extern "C" int gw_engine_is(const char *id) {
	return g_engine && ConfMan.get("engineid").equalsIgnoreCase(id);
}
static bool g_loadAtStart;

/* The engines without a hook of their own: ScummVM's interface for its global menu. A load is
   done from the next pollEvent(), where that menu would do it, not from wherever in the
   engine's drawing the firmware's menu was opened. */
static bool g_loadPending;
static int generic_save(int slot) {
	if (!g_engine || !g_engine->canSaveGameStateCurrently()) return 0;
	return g_engine->saveGameState(slot, "Retro-Go").getCode() == Common::kNoError;
}
static int generic_load(int slot) {
	(void)slot;
	if (!g_engine || !g_engine->canLoadGameStateCurrently()) return 0;
	g_loadPending = true;
	return 1;
}
static void generic_load_now() {
	if (!g_loadPending) return;
	g_loadPending = false;
	if (g_engine && g_engine->loadGameState(RG_SLOT).getCode() != Common::kNoError)
		plat_log("ScummVM: could not load the saved game\n");
}

/* the firmware's save callback: written now, into path */
extern "C" int gw_rg_save(const char *path) {
	Common::strlcpy(g_rgPath, path, sizeof g_rgPath);
	int ok = gw_engine_is("sky") ? gw_sky_save(RG_SLOT) : gw_engine_is("scumm") ? gw_scumm_save(RG_SLOT) : generic_save(RG_SLOT);
	if (!ok) plat_log("ScummVM: cannot save right now\n");
	return ok;
}

/* the firmware's load callback: the engine loads it at the start of its next loop (or, before
   ScummVM has started, at the game's start: plat_rg_load_at_start) -- the path stays set */
extern "C" int gw_rg_load(const char *path) {
	Common::strlcpy(g_rgPath, path, sizeof g_rgPath);
	if (!g_engine) { g_loadAtStart = true; return 1; }
	int ok = gw_engine_is("sky") ? gw_sky_load(RG_SLOT) : gw_engine_is("scumm") ? gw_scumm_load(RG_SLOT) : generic_load(RG_SLOT);
	if (!ok) plat_log("ScummVM: cannot load right now\n");
	return ok;
}

extern "C" int gw_rg_load_at_start(void) { return g_loadAtStart; }

class GWFilesystemFactory : public FilesystemFactory {
public:
	AbstractFSNode *makeRootFileNode() const override { return new GWFSNode(""); }
	AbstractFSNode *makeCurrentDirectoryFileNode() const override { return new GWFSNode(plat_save_path()); }
	AbstractFSNode *makeFileNodePath(const Common::String &path) const override { return new GWFSNode(path); }
};

/* ================================================================ screen */

extern "C" int heap_screen_spill;       /* src/heap.c: may everything use the SCUMM screens' heap? */
#ifndef GW_ENGINE_VOLUME
#define GW_ENGINE_VOLUME 160
#endif

static int g_gwGain = 5;                /* output gain in eighths (GWMixer::pump) */

class GWGraphics : public NullGraphicsManager {
public:
	Graphics::Surface _screen;
	byte _pal[256 * 3];
	// mouse cursor, drawn by the platform when it copies the frame
	byte *_cursor = nullptr;
	uint _cw = 0, _ch = 0;
	int _hotX = 0, _hotY = 0;
	uint32 _key = 0;
	bool _cursorVisible = false;
	int _mouseX = 0, _mouseY = 0;
	bool _mouseSet = false, _mouseTell = false;   // the pointer starts in the middle of the screen
	int _shakeX = 0, _shakeY = 0;

	GWGraphics() { memset(_pal, 0, sizeof _pal); }
	~GWGraphics() override { _screen.free(); free(_cursor); }

	void initSize(uint w, uint h, const Graphics::PixelFormat *format) override {
		NullGraphicsManager::initSize(w, h, format);
		if (_screen.w != (int)w || _screen.h != (int)h) {
			_screen.free();
			_screen.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
		}
		// SCUMM keeps its screens in a heap of their own; no other engine has such screens
		if (!ConfMan.get("engineid").equalsIgnoreCase("scumm")) heap_screen_spill = 1;
		// The core runs ScummVM at full volume (the console has its own control), which suits
		// SCUMM. The other engines' music, effects and voices played together clip at that level
		// (Ween's music: 4.6% of the samples): GW_ENGINE_VOLUME of 255 for them. Beneath a Steel
		// Sky sets its own (patches/scummvm-slim.patch, sky.cpp).
		if (!_mouseSet && !ConfMan.get("engineid").equalsIgnoreCase("scumm") &&
		    !ConfMan.get("engineid").equalsIgnoreCase("sky")) {
			// Lure of the Temptress is the other way round: its AdLib music alone, scaled by
			// the volume twice (driver and mixer), peaks at 2% of full scale. Full volume and
			// four times the output gain for it.
			bool lure = ConfMan.get("engineid").equalsIgnoreCase("lure");
			int v = lure ? 255 : GW_ENGINE_VOLUME;
			if (lure) g_gwGain = 20;
			static const char *const vol[] = { "music_volume", "sfx_volume", "speech_volume" };
			for (const char *k : vol) ConfMan.setInt(k, v, Common::ConfigManager::kTransientDomain);
			Audio::Mixer *m = g_system->getMixer();
			m->setVolumeForSoundType(Audio::Mixer::kMusicSoundType, v);
			m->setVolumeForSoundType(Audio::Mixer::kSFXSoundType, v);
			m->setVolumeForSoundType(Audio::Mixer::kSpeechSoundType, v);
			m->setVolumeForSoundType(Audio::Mixer::kPlainSoundType, v);
		}
		if (!_mouseSet) {
			_mouseSet = _mouseTell = true;       // pollEvent() tells the game where it is
			_mouseX = w / 2;
			_mouseY = h / 2;
		}
	}
	void setPalette(const byte *c, uint start, uint num) override {
		memcpy(_pal + start * 3, c, num * 3);
		plat_set_palette(c, start, num);
	}
	void grabPalette(byte *c, uint start, uint num) const override { memcpy(c, _pal + start * 3, num * 3); }
	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override {
		_screen.copyRectToSurface(buf, pitch, x, y, w, h);
	}
	Graphics::Surface *lockScreen() override { return &_screen; }
	void fillScreen(uint32 col) override { _screen.fillRect(Common::Rect(_screen.w, _screen.h), col); }
	void fillScreen(const Common::Rect &r, uint32 col) override { _screen.fillRect(r, col); }
	void setShakePos(int x, int y) override { _shakeX = x; _shakeY = y; }

	bool showMouse(bool visible) override { bool old = _cursorVisible; _cursorVisible = visible; return old; }
	void warpMouse(int x, int y) override { _mouseX = x; _mouseY = y; }
	void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
	                    const Graphics::PixelFormat *format, const byte *mask, frac_t, frac_t) override {
		free(_cursor);
		_cursor = (byte *)malloc(w * h);
		if (_cursor) memcpy(_cursor, buf, w * h);
		_cw = w; _ch = h; _hotX = hotspotX; _hotY = hotspotY; _key = keycolor;
	}

	void updateScreen() override {
		if (!_screen.getPixels()) return;
		// the cursor goes into the frame for the platform: save what is under it, draw, present, restore
		byte under[64 * 64];
		int cx = _mouseX - _hotX, cy = _mouseY - _hotY;
		bool drawCursor = _cursorVisible && _cursor && _cw <= 64 && _ch <= 64;
		if (drawCursor) {
			for (uint y = 0; y < _ch; y++) for (uint x = 0; x < _cw; x++) {
				int sx = cx + x, sy = cy + y;
				if (sx < 0 || sy < 0 || sx >= _screen.w || sy >= _screen.h) continue;
				byte *p = (byte *)_screen.getBasePtr(sx, sy);
				under[y * 64 + x] = *p;
				byte c = _cursor[y * _cw + x];
				if (c != _key) *p = c;
			}
		}
		plat_present((const uint8_t *)_screen.getPixels(), _screen.pitch, _screen.w, _screen.h);
		if (drawCursor) {
			for (uint y = 0; y < _ch; y++) for (uint x = 0; x < _cw; x++) {
				int sx = cx + x, sy = cy + y;
				if (sx < 0 || sy < 0 || sx >= _screen.w || sy >= _screen.h) continue;
				*(byte *)_screen.getBasePtr(sx, sy) = under[y * 64 + x];
			}
		}
	}
};

/* ================================================================ sound */

class GWMixerManager : public MixerManager {
	enum { CHUNK = 256 };                  // frames mixed at a time
	int16 _stereo[CHUNK * 2];
	int16 _mono[CHUNK];
	bool _busy = false;
public:
	void init() override {
		uint32 rate = plat_audio_rate();
		_mixer = new Audio::MixerImpl(rate ? rate : 22050, true, CHUNK);
		_mixer->setReady(rate != 0);
	}
	void suspendAudio() override { _audioSuspended = true; }
	int resumeAudio() override {
		if (!_audioSuspended) return -2;
		_audioSuspended = false;
		return 0;
	}
	bool isNullDevice() const override { return plat_audio_rate() == 0; }

	void pump() {
		if (!_mixer || _busy || _audioSuspended || !plat_audio_rate()) return;
		_busy = true;                      // the mixer's own callbacks may end up waiting
		while (plat_audio_free() >= CHUNK) {
			_mixer->mixCallback((byte *)_stereo, sizeof _stereo);
			// to mono, a quarter louder than the plain average (the console's speaker is quiet)
			for (int i = 0; i < CHUNK; i++) {
				int v = (((int)_stereo[2 * i] + _stereo[2 * i + 1]) * g_gwGain) >> 3;
				_mono[i] = (int16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
			}
			plat_audio_write(_mono, CHUNK);
		}
		_busy = false;
	}
};

static GWMixerManager *g_mixerManager;
static void pumpAudio() { if (g_mixerManager) g_mixerManager->pump(); }

/* ================================================================ system */

class OSystem_GW : public ModularMixerBackend, public ModularGraphicsBackend, Common::EventSource {
	uint32 _held = 0;              // buttons seen last poll
	int _mx16 = 160 << 4, _my16 = 100 << 4;
	uint32 _moveStart = 0, _lastMove = 0;   // when the D-pad was pressed / last moved the pointer (ms)
	Common::Queue<Common::Event> _queue;
	GWGraphics *gfx() { return (GWGraphics *)_graphicsManager; }
public:
	OSystem_GW() { _fsFactory = new GWFilesystemFactory(); }

	void initBackend() override {
		_timerManager = new DefaultTimerManager();
		_eventManager = new DefaultEventManager(this);
		_savefileManager = new GWSaveFileManager();
		_graphicsManager = new GWGraphics();
		_mixerManager = g_mixerManager = new GWMixerManager();
		_mixerManager->init();
		plat_set_pump(pumpAudio);
		// full volume (ScummVM starts at 192 of 255): the console's own volume is the one to
		// turn down; transient, as from the command line, over whatever scummvm.ini holds
		static const char *const vol[] = { "music_volume", "sfx_volume", "speech_volume" };
		for (const char *k : vol) ConfMan.setInt(k, 255, Common::ConfigManager::kTransientDomain);
		// subtitles on unless a game was switched to voice only (Ctrl+T, the keyboard's Txt key)
		ConfMan.registerDefault("subtitles", true);
		BaseBackend::initBackend();
	}

	Common::Path getDefaultConfigFileName() override {
		return Common::Path(Common::String(plat_save_path()) + "/scummvm.ini");
	}

	void push(Common::EventType t, Common::KeyCode k = Common::KEYCODE_INVALID, uint16 ascii = 0) {
		Common::Event e;
		e.type = t;
		e.mouse = Common::Point(gfx()->_mouseX, gfx()->_mouseY);
		if (k != Common::KEYCODE_INVALID) e.kbd = Common::KeyState(k, ascii);
		_queue.push(e);
	}

	static void oskEmit(int keycode, int ascii, int mods, void *arg) {
		OSystem_GW *self = (OSystem_GW *)arg;
		Common::KeyState ks((Common::KeyCode)keycode, (uint16)ascii,
		                    (mods & OSK_SHIFT ? Common::KBD_SHIFT : 0) | (mods & OSK_CTRL ? Common::KBD_CTRL : 0));
		Common::Event e;
		e.mouse = Common::Point(self->gfx()->_mouseX, self->gfx()->_mouseY);
		e.kbd = ks;
		e.type = Common::EVENT_KEYDOWN;
		self->_queue.push(e);
		e.type = Common::EVENT_KEYUP;
		self->_queue.push(e);
	}

	void readButtons() {
		uint32 b = plat_buttons(), down = b & ~_held, up = _held & ~b;
		_held = b;
		// GAME: the on-screen keyboard (osk.c). While it is open, the D-pad, A and B are its
		if (down & PLAT_GAME) {
			if (osk_is_open()) osk_close(); else osk_open();
			plat_redraw();
			return;
		}
		if (osk_is_open()) {
			if (osk_input(down, PLAT_UP, PLAT_DOWN, PLAT_LEFT, PLAT_RIGHT, PLAT_A, PLAT_B, oskEmit, this))
				plat_redraw();
			return;
		}
		// D-pad: a mouse that speeds up while held. Driven by time, not by polls: ScummVM polls
		// far more often on the console than in an emulator, and a per-poll step sent the
		// pointer flying to the edges. 60 px/s at first, up to 240 px/s after ~0.7 s.
		int dx = (b & PLAT_RIGHT ? 1 : 0) - (b & PLAT_LEFT ? 1 : 0);
		int dy = (b & PLAT_DOWN ? 1 : 0) - (b & PLAT_UP ? 1 : 0);
		uint32 now = plat_millis();
		if (dx || dy) {
			GWGraphics *g = gfx();
			if (!_moveStart) {
				_moveStart = _lastMove = now;
				_mx16 = g->_mouseX << 4; _my16 = g->_mouseY << 4;   // the game may have warped it
				// a tap moves one pixel, for fine aiming
				_mx16 += dx << 4; _my16 += dy << 4;
			}
			uint32 held = now - _moveStart, dt = now - _lastMove;
			if (dt > 50) dt = 50;
			int speed = 60 + (int)(held > 700 ? 180 : held * 180 / 700);    // px/s
			int step = speed * 16 * (int)dt / 1000;                             // 1/16 px
			if (step) _lastMove = now;          // else the time keeps adding up until it moves
			int w = g->_screen.w ? g->_screen.w : 320, h = g->_screen.h ? g->_screen.h : 200;
			_mx16 = CLIP(_mx16 + dx * step, 0, (w - 1) << 4);
			_my16 = CLIP(_my16 + dy * step, 0, (h - 1) << 4);
			if ((_mx16 >> 4) != g->_mouseX || (_my16 >> 4) != g->_mouseY) {
				g->_mouseX = _mx16 >> 4; g->_mouseY = _my16 >> 4;
				push(Common::EVENT_MOUSEMOVE);
			}
		} else
			_moveStart = 0;
		// B = left click (the main button, under the thumb), A = right click
		if (down & PLAT_B) push(Common::EVENT_LBUTTONDOWN);
		if (up & PLAT_B) push(Common::EVENT_LBUTTONUP);
		if (down & PLAT_A) push(Common::EVENT_RBUTTONDOWN);
		if (up & PLAT_A) push(Common::EVENT_RBUTTONUP);
		// TIME: F5, the game's own menu (save with a name, load, quit...)
		if (down & PLAT_TIME) push(Common::EVENT_KEYDOWN, Common::KEYCODE_F5, Common::ASCII_F5);
		if (up & PLAT_TIME) push(Common::EVENT_KEYUP, Common::KEYCODE_F5, Common::ASCII_F5);
		// Zelda units: START skips a cut-scene (Esc), SELECT the current line of dialogue ('.');
		// on a Mario unit both are on the keyboard
		if (down & PLAT_START) push(Common::EVENT_KEYDOWN, Common::KEYCODE_ESCAPE, Common::ASCII_ESCAPE);
		if (up & PLAT_START) push(Common::EVENT_KEYUP, Common::KEYCODE_ESCAPE, Common::ASCII_ESCAPE);
		if (down & PLAT_SELECT) push(Common::EVENT_KEYDOWN, Common::KEYCODE_PERIOD, '.');
		if (up & PLAT_SELECT) push(Common::EVENT_KEYUP, Common::KEYCODE_PERIOD, '.');
	}

	bool pollEvent(Common::Event &event) override {
		((DefaultTimerManager *)getTimerManager())->checkTimers();
		pumpAudio();
		generic_load_now();
		if (gfx()->_mouseTell) {
			gfx()->_mouseTell = false;
			push(Common::EVENT_MOUSEMOVE);
		}
		if (_queue.empty()) readButtons();
		if (_queue.empty()) return false;
		event = _queue.pop();
		return true;
	}

	Common::MutexInternal *createMutex() override { return new NullMutexInternal(); }
	uint32 getMillis(bool) override { return plat_millis(); }
	void delayMillis(uint ms) override {
		plat_delay(ms);
		((DefaultTimerManager *)getTimerManager())->checkTimers();
	}
	void getTimeAndDate(TimeDate &td, bool) const override {
		plat_date(&td.tm_year, &td.tm_mon, &td.tm_mday, &td.tm_hour, &td.tm_min, &td.tm_sec, &td.tm_wday);
	}
	void quit() override { plat_quit(); }
	void fatalError() override { plat_fatal(); }
	void logMessage(LogMessageType::Type, const char *message) override { plat_log(message); }
	void addSysArchivesToSearchSet(Common::SearchSet &, int) override {}
};

// engines/engine.cpp: the ScummVM splash needs the GUI overlay and its theme (~600 KB); the
// platform shows its own "ScummVM" splash instead (plat_splash), so the engine's is marked done.
extern bool splash;

extern "C" int gw_scummvm_main(int argc, const char *const argv[]) {
	splash = true;
	g_system = new OSystem_GW();
	int res = scummvm_main(argc, argv);
	g_system->destroy();
	return res;
}

/* patches/scummvm-slim.patch (SmushPlayer::play): a game file the engine did not find -- what
   the card says about it, to scummvm.log: the folder's size, the name as it is listed there,
   and what stat() answers for the path the engine asked with. */
namespace {
struct MissingScan { const char *name; int files; uint32 kb; char found[64]; uint32 foundSize; };
void missingCb(const char *name, bool isDir, uint32_t size, void *arg) {
	MissingScan *m = (MissingScan *)arg;
	if (isDir) return;
	m->files++;
	m->kb += size / 1024;
	if (!scumm_stricmp(name, m->name)) { Common::strlcpy(m->found, name, sizeof m->found); m->foundSize = size; }
}
}
void gwLogMissingFile(const char *name) {
	Common::String base = ConfMan.getPath("path").toString('/');
	static const char *const subs[] = { "", "/VIDEO", "/DATA" };
	for (const char *sub : subs) {
		Common::String dir = base + sub;
		MissingScan m = { name, 0, 0, "", 0 };
		bool listed = plat_list_dir(dir.c_str(), missingCb, &m);
		Common::String path = dir + "/" + name;
		plat_stat_t st = plat_stat(path.c_str());
		Common::String line = Common::String::format("ScummVM: %s: %s%d files, %lu KB; listed as '%s' (%lu bytes); stat(%s): %s, %lu bytes\n",
			dir.c_str(), listed ? "" : "CANNOT LIST, ", m.files, (unsigned long)m.kb, m.found, (unsigned long)m.foundSize,
			name, st.exists ? "exists" : "no", (unsigned long)st.size);
		plat_log(line.c_str());
	}
}
