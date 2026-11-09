/*
 * ScummVM's makeVorbisStream() / makeMP3Stream() / makeFLACStream() on the decoders of
 * src/gw_codecs.h, in place of audio/decoders/{vorbis,mp3,flac}.cpp (which need libvorbis,
 * libmad and libFLAC). With USE_VORBIS / USE_MAD / USE_FLAC defined, the SCUMM engine finds
 * the speech of talkie versions (.sog / .so3 / .sof) and the CD audio manager the music tracks
 * (trackN.ogg / .mp3 / .flac).
 */
#include "audio/audiostream.h"
#include "audio/decoders/vorbis.h"
#include "audio/decoders/mp3.h"
#include "audio/decoders/flac.h"
#include "common/stream.h"
#include "gw_codecs.h"

namespace Audio {

class GWCodecStream : public SeekableAudioStream {
	Common::SeekableReadStream *_in;
	DisposeAfterUse::Flag _dispose;
	gwc *_c = nullptr;
	int _rate = 0, _channels = 1;
	bool _end = false;

	static uint32_t ioRead(void *ctx, void *buf, uint32_t n) {
		return ((Common::SeekableReadStream *)ctx)->read(buf, n);
	}
	static int ioSeek(void *ctx, uint32_t pos) {
		return ((Common::SeekableReadStream *)ctx)->seek(pos, SEEK_SET) ? 1 : 0;
	}

public:
	GWCodecStream(int kind, Common::SeekableReadStream *in, DisposeAfterUse::Flag dispose) : _in(in), _dispose(dispose) {
		gwc_io io = { in, ioRead, ioSeek, (uint32_t)in->size() };
		in->seek(0, SEEK_SET);
		_c = gwc_open(kind, &io);
		if (_c) {
			_rate = gwc_rate(_c);
			_channels = gwc_channels(_c);
		}
	}
	~GWCodecStream() override {
		gwc_close(_c);
		if (_dispose == DisposeAfterUse::YES) delete _in;
	}
	bool ok() const { return _c != nullptr; }

	int readBuffer(int16 *buffer, const int numSamples) override {
		if (_end) return 0;
		int want = numSamples / _channels, got = gwc_read(_c, buffer, want);
		if (got < want) _end = true;
		return got * _channels;
	}
	bool isStereo() const override { return _channels == 2; }
	int getRate() const override { return _rate; }
	bool endOfData() const override { return _end; }

	bool seek(const Timestamp &where) override {
		uint32 frame = convertTimeToStreamPos(where, _rate, false).totalNumberOfFrames();
		_end = !gwc_seek(_c, frame);
		return !_end;
	}
	Timestamp getLength() const override { return Timestamp(0, gwc_length(_c), _rate); }
};

static SeekableAudioStream *make(int kind, Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose) {
	if (!stream) return nullptr;
	GWCodecStream *s = new GWCodecStream(kind, stream, dispose);
	if (s && !s->ok()) {
		delete s;                         // disposes of the stream as asked, as ScummVM's do
		return nullptr;
	}
	return s;
}

SeekableAudioStream *makeVorbisStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose) {
	return make(GWC_VORBIS, stream, dispose);
}
SeekableAudioStream *makeMP3Stream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose) {
	return make(GWC_MP3, stream, dispose);
}
SeekableAudioStream *makeFLACStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose) {
	return make(GWC_FLAC, stream, dispose);
}

// MP3 in packets (videos): not used by the SCUMM engine
PacketizedAudioStream *makePacketizedMP3Stream(Common::SeekableReadStream &) { return nullptr; }
PacketizedAudioStream *makePacketizedMP3Stream(uint, uint) { return nullptr; }

} // End of namespace Audio
