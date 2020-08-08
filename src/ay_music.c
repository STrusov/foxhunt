/**\file*********************************************************************
 *                                                                     \brief
 * Воспроизводит написанные на ZX-Spectrum композиции CPS.
 *
 * Эмулятор музыкального процессора AY-3-8912/10, реализованный функциями
 * ay_init() и ay_make_chunk(), заимствован из UnrealSpeсcy by SMT и
 * подпадает под GPL.
 *
 ****************************************************************************
 */

#define _POSIX_C_SOURCE 200809L

#include <alloca.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <threads.h>
#include <alsa/asoundlib.h>

enum {
	A = 0, B, C,	ay_channels,
	L = 0, R,   	channels,

	zx_frame_rate     	= 50,
	ay_clock          	= 1773400,

	// Максимальная громкость учитывает смешивание 3-х каналов с соотв. весами.
	max_vol           	= 0x7fff*100/(100+67+10),

	default_sample_rate	= 48000,
	format             	= SND_PCM_FORMAT_S16,
	chunks             	= 2, ///< Количество фрагментов в буфере звука.
	default_period_time	= 1000000 / zx_frame_rate,
	default_buffer_time	= chunks * default_period_time,
};

static_assert(channels == 2);

static unsigned int	sample_rate	= default_sample_rate;

/** Абстракция регистров музыкального процессора */
static unsigned	tone_cycle    	[ay_channels];
static unsigned	tone_frequency	[ay_channels];
static unsigned	tone_mask     	[ay_channels];
static unsigned	tone_bit      	[ay_channels];
static unsigned	tone_volume   	[ay_channels];
static unsigned	noise_cycle;
static unsigned	noise_frequency;
static unsigned	noise_mask    	[ay_channels];
static unsigned	noise_bit;
static unsigned	noise_state;
static unsigned	envelope_cycle;
static unsigned	envelope_frequency;
static unsigned	envelope_volume;
static int     	envelope_add;
static unsigned	envelope_mode;

typedef struct { int16_t l; int16_t r; } lr32;

/** Отображение возможных громкостей каналов AY на звуковое стереопространство. */
static lr32 volmap[ay_channels][16];

static void ay_init(void)
{
	// Таблица соответствий 16-ти громкостей AY-3-8912 16-ти разрядному звуку.
	static const uint16_t	v[16] = {
		0x0000, 0x0340, 0x04C0, 0x06F2, 0x0A44, 0x0F13, 0x1510, 0x227E,
		0x289F, 0x414E, 0x5B21, 0x7258, 0x905E, 0xB550, 0xD7A0, 0xFFFF
	};
	//  Громкость эмулируемого канала в звуковом стереопространстве.
	static const uint16_t	stereo[ay_channels][channels] = {
		//    левый     	    правый     	// каналы
		{ 100*0x100/100,	010*0x100/100},	// A
		{ 066*0x100/100,	066*0x100/100},	// B
		{ 010*0x100/100,	100*0x100/100} 	// C
	};
	for (int j = A; j <= C; ++j)
		for (int i = 0; i < 16; ++i) {
			volmap[j][i].l = ((v[i] * max_vol >> 16) * stereo[j][L] >> 8);
			volmap[j][i].r = ((v[i] * max_vol >> 16) * stereo[j][R] >> 8);
		}
	noise_state = 0xFFFF;
}

/** Сюда генерируем сэмпл. */
lr32    	*chunk;
unsigned	chunk_size;

/** Формируем фрагмент PCM звука длительностью один кадр (1/50 сек) ZX-Spectrum. */
static void ay_make_chunk(void)
{
	static_assert((int)format == SND_PCM_FORMAT_S16);
	//  Такты AY
	for (int t = 0; t < ay_clock / zx_frame_rate; t += 8) {
		if (++noise_cycle >= noise_frequency) {
			noise_cycle = 0;
			noise_state = (noise_state * 2 + 1)
			            ^ (((noise_state >> 16) ^ (noise_state >> 13)) & 1);
			noise_bit = - ((noise_state >> 16) & 1);
		}
		if (++envelope_cycle >= envelope_frequency) {
			envelope_cycle = 0;
			envelope_volume += envelope_add;
			if (envelope_volume & ~0x1F) {
				unsigned mask = 1 << envelope_mode;
				if (mask & (0x0F | 1<<9 | 1<<15)) {
					envelope_volume = envelope_add = 0;
				} else if (mask & (1<<8 | 1<<12)) {
					envelope_volume &= 0x1F;
				} else if (mask & (1<<10 | 1<<14)) {
					envelope_add = -envelope_add;
					envelope_volume += envelope_add;
				} else { // mask 11 | 13
					envelope_volume = 0x1F;
					envelope_add = 0;
				}
			}
		}
		lr32 quant = { 0 };
		for (int chn = A; chn <= C; ++chn) {
			if (++tone_cycle[chn] >= tone_frequency[chn]) {
				tone_cycle[chn] = 0;
				tone_bit[chn] ^= -1;
			}
			unsigned env, bit;
			env = tone_volume[chn] & 0x10
			    ? envelope_volume / 2 : tone_volume[chn] & 0x0F;
			bit = (tone_bit[chn] | tone_mask[chn]) & (noise_bit | noise_mask[chn]);
			// в SND_PCM_FORMAT_S16 диапазон -/+ volmap
			quant.l += volmap[chn][env].l ^ bit;
			quant.r += volmap[chn][env].r ^ bit;
		}
		unsigned pos = t * sample_rate / ay_clock;
		assert(pos < chunk_size);
		if (pos < chunk_size) {
			chunk[pos] = quant;
		}
	}
}


static snd_pcm_t *pcm = NULL;

static unsigned int     	period_time	= default_period_time;
static unsigned int     	buffer_time	= default_buffer_time;
static snd_pcm_uframes_t	period_size;
static snd_pcm_uframes_t	buffer_size; ///< Внутренний буфер

// В случае PulseAudio устройство "hw" не выводит звук, а для "default"
// заявляются все возможные форматы. Что бы избежать передискретизации
// выбираем формат для "hw" и задаём его для "default".
static int pcm_init(void)
{
	int r, dir = 0;
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_hw_params_alloca(&hwparams);
	static const char *dev[2] = { "hw", "default" };
	for (int i = 0; i < 2; ++i) {
		snd_pcm_t *pcm0 = pcm;
		r = snd_pcm_open(&pcm, dev[i], SND_PCM_STREAM_PLAYBACK, 0);
		if (r >= 0) {
			if (i == 1 && pcm0)
				snd_pcm_close(pcm0);
			snd_pcm_hw_params_any(pcm, hwparams);
			snd_pcm_hw_params_set_rate_resample(pcm, hwparams, 0);
			snd_pcm_hw_params_set_access(pcm, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
			snd_pcm_hw_params_set_format(pcm, hwparams, format);
			snd_pcm_hw_params_set_channels(pcm, hwparams, channels);
			snd_pcm_hw_params_set_rate_near(pcm, hwparams, &sample_rate, &dir);
			snd_pcm_hw_params_set_period_time_near(pcm, hwparams, &period_time, &dir);
			snd_pcm_hw_params_get_period_size(hwparams, &period_size, &dir);
			snd_pcm_hw_params_set_buffer_time_near(pcm, hwparams, &buffer_time, &dir);
			snd_pcm_hw_params_get_buffer_size(hwparams, &buffer_size);
			r = snd_pcm_hw_params(pcm, hwparams);
		}
		printf("Звуковое устройство %s: %u канала, %u Гц (буфер %lu дискретизаций, сэмпл %g мс).\n",
		       r >= 0 ? dev[i] : snd_strerror(r),
		       channels, sample_rate, buffer_size, period_time/1000.0);
	}
	snd_pcm_sw_params_t *swparams;
	snd_pcm_sw_params_alloca(&swparams);
	snd_pcm_sw_params_set_start_threshold(pcm, swparams, period_size);
	snd_pcm_sw_params_set_avail_min(pcm, swparams, period_size);
	snd_pcm_sw_params(pcm, swparams);
	return r;
}

void pcm_stop(void)
{
	snd_pcm_close(pcm);
}

bool pcm_play_chunk(lr32 *buff, snd_pcm_uframes_t size)
{
	while (size > 0) {
		int r = snd_pcm_writei(pcm, buff, size);
		if (r >= 0) {
			buff += r;
			size -= r;
			continue;
		}
		switch (r) {
		case -ESTRPIPE:
			while ((r = snd_pcm_resume(pcm)) == -EAGAIN)
				sleep(1);
		case -EPIPE:
			snd_pcm_prepare(pcm);
		case -EAGAIN:
			continue;
		// TODO переинициализировать?
		case -ENODEV:
		case -ENOTTY:
		case -EBADFD:
		default:
			sleep(1);
			return false;
		}
	}
	return true;
}


thrd_t player;
cnd_t  wake;
mtx_t  wake_mtx;
int    ticks;
bool   pausable;
bool   exit_player;

int ay_music_init(void)
{
	ay_init();
	int r = pcm_init();
	if (r >= 0)
		printf("Создан эмулятор музыкального процессора AY-3-8912.\n");
	chunk_size = sample_rate * default_period_time / 1000000;
	chunk = malloc(chunk_size * channels * sizeof(int16_t));
	return r;
}

void ay_music_stop(void)
{
	exit_player = true;
	thrd_join(player, NULL);
	if (!pausable)
		mtx_destroy(&wake_mtx);
	free(chunk);
	pcm_stop();
}

void ay_music_continue(int t)
{
	if (!pausable) {
		mtx_init(&wake_mtx, mtx_plain);
		pausable = true;
	}
	ticks = t;
	cnd_signal(&wake);
}

int music_thread(void*);

void ay_music_play(void)
{
	thrd_create(&player, music_thread, NULL);
}

const uint8_t music[] = {
#include "../music/foxh.cps.inl"
};
static_assert(sizeof(music));

typedef struct uint16_le {
	uint8_t 	l;
	uint8_t 	h;
} uint16_le;

static inline uint16_t le16(const struct uint16_le *le)
{
	return le->l + (le->h << 8);
}

/** Заголовок композиции */
struct compose {
	// Сигнатура компилятора и сведения об авторстве.
	char     	signature[0x10];
	uint8_t  	title_strlen; 	// 0x10
	char     	title[0x0F];
	uint8_t  	author_srtlen;	// 0x20
	char     	author[0x0F];
	// Данные
	uint8_t  	loop_position;	// 0x30
	uint8_t  	positions;    	// 0x31
	uint8_t  	mode;         	// 0x32 Не используется
	uint16_le	loop;         	// 0x33
	uint16_le	pattern_table;	// 0x35
	uint16_le	sample_table; 	// 0x37
	uint8_t  	patterns;     	// 0x39 Не используется
	uint8_t  	element[];    	// 0x3A
};

struct pattern {
	uint8_t  	length;       	// 0x00
	uint16_le	envelope_data;	// 0x01..0x02
	uint16_le	channel[ay_channels];
};

struct sample {
	uint8_t  	length;     	// 0x00
	uint16_le	loop;       	// 0x01..0x02
	uint8_t  	repeat;     	// 0x03
	uint8_t  	data[];     	// 0x04
};

struct ornament {
	uint8_t  	length;     	// 0x00
	uint8_t  	loop;       	// 0x01
	uint8_t  	repeat;     	// 0x02
	uint8_t  	data[];     	// 0x03
};

struct channel {
	const struct sample  	*sample;
	const uint8_t        	*sample_data;
	const struct ornament	*ornament;
	unsigned     	pattern_elm;
	uint8_t      	repeat;
	uint8_t      	note;
	uint8_t      	so;
	uint8_t      	quark;   	// 8-ми разрядный счётчик, обнуляется при переполнении.
	uint8_t      	sample_length;
	uint8_t      	orn_length;
	uint8_t      	orn_line;	// 8-ми разрядный счётчик, обнуляется при переполнении.
	uint8_t      	tone_disp;
};

int music_thread(void *p)
{
	static const uint16_t base_frequency[12] = {
		0xEF8, 0xE10, 0xD60, 0xC80, 0xBD8, 0xB28,
		0xA88, 0x9F0, 0x960, 0x8E0, 0x858, 0x7E0
	};
	unsigned frequency_div[8*12+1], *pfq = frequency_div;
	for (int octave = 0; octave < 8; ++octave) {
		for (int semitone = 0; semitone < 12; ++semitone)
			*pfq++ = base_frequency[semitone] >> octave;
	}
	*pfq = 0;

	const uint8_t *data = music;
	const struct compose *hdr  = (void*)data;
	struct channel channel[ay_channels] = {0};
	unsigned position = 0;
	unsigned eidx  = 0;
	unsigned delay = 0;

play_music:
	if (position >= hdr->positions) {
		position = hdr->loop_position;
		eidx = le16(&hdr->loop) - offsetof(struct compose, element);
	}
	if (hdr->element[eidx] & 1<<6)
		delay = hdr->element[eidx++] & 0x3F;
	assert(delay);
	const struct pattern *pt = (const void*)&data[le16(&hdr->pattern_table)];
	const struct pattern *pattern = &pt[hdr->element[eidx] & 0x7F];
	unsigned env_idx = le16(&pattern->envelope_data);
	unsigned env_repeat = 1;
	unsigned env_freq = 0;
	channel[A].pattern_elm = le16(&pattern->channel[A]);
	channel[B].pattern_elm = le16(&pattern->channel[B]);
	channel[C].pattern_elm = le16(&pattern->channel[C]);
	channel[A].repeat = channel[B].repeat = channel[C].repeat = 1;
	if (hdr->element[eidx] & 1<<7) {
		channel[A].tone_disp = hdr->element[++eidx];
		channel[B].tone_disp = hdr->element[++eidx];
		channel[C].tone_disp = hdr->element[++eidx];
	} else {
		channel[A].tone_disp = 0;
		channel[B].tone_disp = 0;
		channel[C].tone_disp = 0;
	}
	++eidx;
	++position;
	for (unsigned line = 0; line <= pattern->length; ++line) {
		if (!(--env_repeat)) {
			if (data[env_idx]) {
				env_freq = data[env_idx++];
				env_repeat = 1;
			} else {
				env_idx++;
				env_repeat = data[env_idx++];
			}
		}
		// Продолжаем играть предыдущую ноту или генерируем новую.
		for (int cn = A; cn <= C; ++cn) {
			if (--channel[cn].repeat)
				continue;
			uint8_t elm = data[channel[cn].pattern_elm++];
			if ((elm & 0x7f) >= 96) {
				channel[cn].repeat = elm < 0x80 ? elm - 95 : elm - 95 - 96;
			} else {
				channel[cn].note   = elm & 0x7F;
				channel[cn].repeat = 1;
				channel[cn].quark  = -1;
				if (elm & 0x80)	{
					// Sample + Ornament byte
					uint8_t so = data[channel[cn].pattern_elm++];
					if (!so)
						channel[cn].note = 0xFF;
					if (so & 0x0F)
						channel[cn].so = (channel[cn].so&0xF0) | (so&0x0F);
					if (so & 0xF0)
						channel[cn].so = (channel[cn].so&0x0F) | (so&0xF0);
				}
				uint8_t so = channel[cn].so;
				// Таблица инструментов содержит заглушки для 0-х сэмпла и орнамента.
				const uint16_le *instrument = (const void*)&data[le16(&hdr->sample_table)];
				channel[cn].sample = (const void*)&data[le16(&instrument[so >> 4])];
				channel[cn].sample_length = channel[cn].sample->length;
				channel[cn].sample_data  = &channel[cn].sample->data[0];
				channel[cn].ornament = (const void*)&data[le16(&instrument[16 + (so & 0xF)])];
				channel[cn].orn_length = channel[cn].ornament->length;
				channel[cn].orn_line   = 0;
			}
		}
		// Воспроизводим delay квантов.
		for (unsigned i = 0; i < delay; ++i) {
			for (int cn = A; cn <= C; ++cn) {
				tone_mask[cn]  = 0;
				noise_mask[cn] = -1;
				if (channel[cn].note >= 0x80) {
					tone_volume[cn] = 0;
					tone_mask[cn] = -1;
				} else {
					uint8_t note = 0x7F & (channel[cn].note + channel[cn].tone_disp
					                     + channel[cn].ornament->data[channel[cn].orn_line]);
					if (note > 96)
						note = 97;
					unsigned voltone = channel[cn].sample_data[0]
					                 + channel[cn].sample_data[1] * 0x100;
					channel[cn].sample_data += 2;
					tone_frequency[cn] = 0xFFF & (frequency_div[note] + voltone);
					if ((voltone & 0xFFF) == 0x7FF) {
						tone_mask[cn] = -1;
						tone_frequency[cn] = 0;
					}
					if ((voltone & 0xFFF) == 0x800) {
						tone_frequency[cn] = 0;
					}
					uint8_t noise = *channel[cn].sample_data++;
					if (noise & 1<<6) {
						unsigned env_mode = voltone >> (8+4);
						if (env_mode) {
							envelope_mode = env_mode;
							envelope_cycle = 0;
							envelope_volume = 0,
							envelope_add = 1;     	// attack
							if (!(env_mode & 4)) {
								envelope_volume += 31;
								envelope_add = -1;	// decay
							}
						}
						tone_volume[cn] = 0x10;
						if (noise & 1<<7) {
							unsigned envd = *channel[cn].sample_data++;
							if (env_freq)
								envelope_frequency = envd + env_freq;
						} else if (env_freq) {
							envelope_frequency = env_freq;
						}
					} else {
						tone_volume[cn] = voltone >> (8+4);
					}
					if (noise & 1<<5) {
						noise_frequency = (noise & 0x1F) * 2;
						noise_mask[cn] = 0;
					}
					channel[cn].quark = (channel[cn].quark + 1) & 0xff;
					if (channel[cn].quark >= channel[cn].sample_length) {
						channel[cn].quark = 0;
						channel[cn].sample_length = channel[cn].sample->repeat;
						channel[cn].sample_data = &data[le16(&channel[cn].sample->loop)];
					}
					if (channel[cn].orn_line >= channel[cn].orn_length) {
						channel[cn].orn_length = channel[cn].ornament->repeat;
						// Признаком перехода на начало является 0xFF
						channel[cn].orn_line = channel[cn].ornament->loop;
					}
					channel[cn].orn_line = (channel[cn].orn_line + 1) & 0xff;
				}
			} // каналы
			if (exit_player)
				return 0;
			// Приостанавливаем воспроизведение, пока основной поток не возобновит.
			if (pausable && --ticks <= 0) {
				cnd_wait(&wake, &wake_mtx);
			}
			ay_make_chunk();
			pcm_play_chunk(chunk, chunk_size);
		} // for (i < delay)
	} // for (line)
goto play_music;
}

