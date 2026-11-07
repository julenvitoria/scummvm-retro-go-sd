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
#include "platform.h"

/* ================================================================ files */

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
		const uint8_t *map = plat_map_file(_path.c_str(), &size);
		if (map) return new Common::MemoryReadStream(map, size, DisposeAfterUse::NO);
		void *f = plat_fopen(_path.c_str(), "rb");
		return f ? new GWFileStream(f, st().size) : nullptr;
	}
	Common::SeekableWriteStream *createWriteStream(bool atomic) override {
		void *f = plat_fopen(_path.c_str(), "wb");
		_statted = false;
		return f ? new GWFileStream(f, 0) : nullptr;
	}
	bool createDirectory() override { _statted = false; return plat_mkdir(_path.c_str()); }
};

class GWFilesystemFactory : public FilesystemFactory {
public:
	AbstractFSNode *makeRootFileNode() const override { return new GWFSNode(""); }
	AbstractFSNode *makeCurrentDirectoryFileNode() const override { return new GWFSNode(plat_save_path()); }
	AbstractFSNode *makeFileNodePath(const Common::String &path) const override { return new GWFSNode(path); }
};

/* ================================================================ screen */

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
	int _shakeX = 0, _shakeY = 0;

	GWGraphics() { memset(_pal, 0, sizeof _pal); }
	~GWGraphics() override { _screen.free(); free(_cursor); }

	void initSize(uint w, uint h, const Graphics::PixelFormat *format) override {
		NullGraphicsManager::initSize(w, h, format);
		if (_screen.w != (int)w || _screen.h != (int)h) {
			_screen.free();
			_screen.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
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
			for (int i = 0; i < CHUNK; i++) _mono[i] = (int16)(((int)_stereo[2 * i] + _stereo[2 * i + 1]) >> 1);
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
		_savefileManager = new DefaultSaveFileManager(Common::Path(plat_save_path()));
		_graphicsManager = new GWGraphics();
		_mixerManager = g_mixerManager = new GWMixerManager();
		_mixerManager->init();
		plat_set_pump(pumpAudio);
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

	void readButtons() {
		uint32 b = plat_buttons(), down = b & ~_held, up = _held & ~b;
		_held = b;
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
		// GAME skips a cut-scene (Esc), TIME the current line of dialogue ('.')
		if (down & PLAT_GAME) push(Common::EVENT_KEYDOWN, Common::KEYCODE_ESCAPE, Common::ASCII_ESCAPE);
		if (up & PLAT_GAME) push(Common::EVENT_KEYUP, Common::KEYCODE_ESCAPE, Common::ASCII_ESCAPE);
		/* TIME: '.' (skip the current line of dialogue) and Enter ("press ENTER to continue") */
		if (down & PLAT_TIME) {
			push(Common::EVENT_KEYDOWN, Common::KEYCODE_PERIOD, '.');
			push(Common::EVENT_KEYUP, Common::KEYCODE_PERIOD, '.');
			push(Common::EVENT_KEYDOWN, Common::KEYCODE_RETURN, Common::ASCII_RETURN);
		}
		if (up & PLAT_TIME) push(Common::EVENT_KEYUP, Common::KEYCODE_RETURN, Common::ASCII_RETURN);
	}

	bool pollEvent(Common::Event &event) override {
		((DefaultTimerManager *)getTimerManager())->checkTimers();
		pumpAudio();
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
