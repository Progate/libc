#ifndef _LINUX_SOUNDCARD_H
#define _LINUX_SOUNDCARD_H

/*
 * OSS（Open Sound System）の /dev/dsp の取り決め。musl の <sys/soundcard.h> はこれを読む。
 *
 * BrowserOS のカーネルは、Linux の上の OSS 4.x（4Front Technologies の Open Sound System）と同じ /dev/dsp を持つ
 * （@progate/browser-os の audio/output.ts）。Linux カーネルの UAPI の soundcard.h は OSS 3 の分だけで、
 * Firefox の cubeb の oss の backend が使う OSS 4 の ioctl（SNDCTL_SYSINFO・SNDCTL_AUDIOINFO・LOW_WATER・
 * SET_CHNORDER）と構造体を持たないので、OSS 4.x の soundcard.h と同じ定義をここに置く。
 *
 * 番号は Linux の _IOR / _IOW / _IOWR で作る（向きのビットは BSD と逆）。形式の値は 4Front の OSS 4.x のもので、
 * AFMT_FLOAT は 0x00004000（FreeBSD の soundcard.h では同じ値が AFMT_U32_LE を指す）。
 * 定義の出どころ: Open Sound System 4.x の soundcard.h（Copyright 4Front Technologies 1993-2006）と、
 * その API を写した FreeBSD の sys/sys/soundcard.h（BSD-2-Clause）
 */

#include <sys/ioctl.h>

#define SOUND_VERSION 0x040000
#define OPEN_SOUND_SYSTEM

/* 標本の形式（SNDCTL_DSP_SETFMT・SNDCTL_DSP_GETFMTS） */
#define AFMT_QUERY 0x00000000
#define AFMT_MU_LAW 0x00000001
#define AFMT_A_LAW 0x00000002
#define AFMT_IMA_ADPCM 0x00000004
#define AFMT_U8 0x00000008
#define AFMT_S16_LE 0x00000010
#define AFMT_S16_BE 0x00000020
#define AFMT_S8 0x00000040
#define AFMT_U16_LE 0x00000080
#define AFMT_U16_BE 0x00000100
#define AFMT_MPEG 0x00000200
#define AFMT_AC3 0x00000400
#define AFMT_S32_LE 0x00001000
#define AFMT_S32_BE 0x00002000
#define AFMT_FLOAT 0x00004000
/* wasm32 はリトルエンディアン */
#define AFMT_S16_NE AFMT_S16_LE
#define AFMT_U16_NE AFMT_U16_LE
#define AFMT_S32_NE AFMT_S32_LE
#define AFMT_S16_OE AFMT_S16_BE
#define AFMT_U16_OE AFMT_U16_BE
#define AFMT_S32_OE AFMT_S32_BE

typedef struct audio_buf_info {
	int fragments;  /* まるごと空いている断片の数 */
	int fragstotal; /* 断片の総数 */
	int fragsize;   /* 断片 1 つのバイト数 */
	int bytes;      /* 空いているバイト数 */
} audio_buf_info;

typedef struct count_info {
	int bytes;  /* 鳴らした（録った）バイトの総数 */
	int blocks; /* 前に訊いてから断片の境を越えた回数 */
	int ptr;    /* バッファの中の位置 */
} count_info;

#define SNDCTL_DSP_HALT _IO('P', 0)
#define SNDCTL_DSP_RESET SNDCTL_DSP_HALT
#define SNDCTL_DSP_SYNC _IO('P', 1)
#define SNDCTL_DSP_SPEED _IOWR('P', 2, int)
#define SNDCTL_DSP_STEREO _IOWR('P', 3, int)
#define SNDCTL_DSP_GETBLKSIZE _IOR('P', 4, int)
#define SNDCTL_DSP_SETFMT _IOWR('P', 5, int)
#define SNDCTL_DSP_SAMPLESIZE SNDCTL_DSP_SETFMT
#define SNDCTL_DSP_CHANNELS _IOWR('P', 6, int)
#define SOUND_PCM_WRITE_CHANNELS SNDCTL_DSP_CHANNELS
#define SNDCTL_DSP_POST _IO('P', 8)
#define SNDCTL_DSP_SETFRAGMENT _IOWR('P', 10, int)
#define SNDCTL_DSP_GETFMTS _IOR('P', 11, int)
#define SNDCTL_DSP_GETOSPACE _IOR('P', 12, audio_buf_info)
#define SNDCTL_DSP_GETISPACE _IOR('P', 13, audio_buf_info)
#define SNDCTL_DSP_NONBLOCK _IO('P', 14)
#define SNDCTL_DSP_GETCAPS _IOR('P', 15, int)
#define SNDCTL_DSP_GETTRIGGER _IOR('P', 16, int)
#define SNDCTL_DSP_SETTRIGGER _IOW('P', 16, int)
#define SNDCTL_DSP_GETIPTR _IOR('P', 17, count_info)
#define SNDCTL_DSP_GETOPTR _IOR('P', 18, count_info)
#define SNDCTL_DSP_GETODELAY _IOR('P', 23, int)
#define SNDCTL_DSP_SILENCE _IO('P', 31)
#define SNDCTL_DSP_SKIP _IO('P', 32)
#define SNDCTL_DSP_HALT_INPUT _IO('P', 33)
#define SNDCTL_DSP_RESET_INPUT SNDCTL_DSP_HALT_INPUT
#define SNDCTL_DSP_HALT_OUTPUT _IO('P', 34)
#define SNDCTL_DSP_RESET_OUTPUT SNDCTL_DSP_HALT_OUTPUT
#define SNDCTL_DSP_LOW_WATER _IOW('P', 34, int)
#define SNDCTL_DSP_GET_CHNORDER _IOR('P', 42, unsigned long long)
#define SNDCTL_DSP_SET_CHNORDER _IOWR('P', 42, unsigned long long)

/* SNDCTL_DSP_GETCAPS */
#define PCM_CAP_REVISION 0x000000ff
#define PCM_CAP_DUPLEX 0x00000100
#define PCM_CAP_REALTIME 0x00000200
#define PCM_CAP_BATCH 0x00000400
#define PCM_CAP_COPROC 0x00000800
#define PCM_CAP_TRIGGER 0x00001000
#define PCM_CAP_MMAP 0x00002000
#define PCM_CAP_MULTI 0x00004000
#define PCM_CAP_BIND 0x00008000
#define PCM_CAP_INPUT 0x00010000
#define PCM_CAP_OUTPUT 0x00020000
#define PCM_CAP_VIRTUAL 0x00040000
#define DSP_CAP_REVISION PCM_CAP_REVISION
#define DSP_CAP_DUPLEX PCM_CAP_DUPLEX
#define DSP_CAP_REALTIME PCM_CAP_REALTIME
#define DSP_CAP_BATCH PCM_CAP_BATCH
#define DSP_CAP_COPROC PCM_CAP_COPROC
#define DSP_CAP_TRIGGER PCM_CAP_TRIGGER
#define DSP_CAP_MMAP PCM_CAP_MMAP
#define DSP_CAP_MULTI PCM_CAP_MULTI
#define DSP_CAP_BIND PCM_CAP_BIND
#define DSP_CAP_INPUT PCM_CAP_INPUT
#define DSP_CAP_OUTPUT PCM_CAP_OUTPUT
#define DSP_CAP_VIRTUAL PCM_CAP_VIRTUAL

/* SNDCTL_DSP_SETTRIGGER と oss_audioinfo.busy */
#define PCM_ENABLE_INPUT 0x00000001
#define PCM_ENABLE_OUTPUT 0x00000002
#define OPEN_READ PCM_ENABLE_INPUT
#define OPEN_WRITE PCM_ENABLE_OUTPUT
#define OPEN_READWRITE (OPEN_READ | OPEN_WRITE)

/* チャンネルの並び（SNDCTL_DSP_SET_CHNORDER）。4 ビットごとに 1 チャンネル */
#define CHID_UNDEF 0
#define CHID_L 1
#define CHID_R 2
#define CHID_C 3
#define CHID_LFE 4
#define CHID_LS 5
#define CHID_RS 6
#define CHID_LR 7
#define CHID_RR 8
#define CHNORDER_UNDEF 0x0000000000000000ULL
#define CHNORDER_NORMAL 0x0000000087654321ULL

#define OSS_LONGNAME_SIZE 64
#define OSS_LABEL_SIZE 16
#define OSS_DEVNODE_SIZE 32
typedef char oss_longname_t[OSS_LONGNAME_SIZE];
typedef char oss_label_t[OSS_LABEL_SIZE];
typedef char oss_devnode_t[OSS_DEVNODE_SIZE];

typedef struct oss_sysinfo {
	char product[32];
	char version[32];
	int versionnum;
	char options[128];
	int numaudios;
	int openedaudio[8];
	int numsynths;
	int nummidis;
	int numtimers;
	int nummixers;
	int openedmidi[8];
	int numcards;
	int numaudioengines;
	char license[16];
	char revision_info[256];
	int filler[172];
} oss_sysinfo;

#define OSS_MAX_SAMPLE_RATES 20
typedef struct oss_audioinfo {
	int dev;
	char name[64];
	int busy;
	int pid;
	int caps;
	int iformats;
	int oformats;
	int magic;
	char cmd[64];
	int card_number;
	int port_number;
	int mixer_dev;
	int legacy_device;
	int enabled;
	int flags;
	int min_rate;
	int max_rate;
	int min_channels;
	int max_channels;
	int binding;
	int rate_source;
	char handle[32];
	unsigned int nrates;
	unsigned int rates[OSS_MAX_SAMPLE_RATES];
	oss_longname_t song_name;
	oss_label_t label;
	int latency;
	oss_devnode_t devnode;
	int next_play_engine;
	int next_rec_engine;
	int filler[184];
} oss_audioinfo;

typedef struct oss_card_info {
	int card;
	char shortname[16];
	char longname[128];
	int flags;
	char hw_info[400];
	int intr_count, ack_count;
	int filler[154];
} oss_card_info;

#define OSS_GETVERSION _IOR('M', 118, int)
#define SNDCTL_SYSINFO _IOR('X', 1, oss_sysinfo)
#define OSS_SYSINFO SNDCTL_SYSINFO
#define SNDCTL_AUDIOINFO _IOWR('X', 7, oss_audioinfo)
#define SNDCTL_CARDINFO _IOWR('X', 11, oss_card_info)
#define SNDCTL_ENGINEINFO _IOWR('X', 12, oss_audioinfo)
#define SNDCTL_AUDIOINFO_EX _IOWR('X', 13, oss_audioinfo)

#endif
