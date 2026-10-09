#ifndef JELLI_AGENT_LOKI_H
#define JELLI_AGENT_LOKI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* loki/1, the link between Loki Agent on the board and Loki Bridge on the Mac
 * (docs/loki-protocol.md, RFC-003). The board decodes what the bridge sends and
 * encodes its own messages. Portable and allocation-free; buffers are caller-owned. */

#define JELLI_LOKI_MAX_IN 2048u /* Largest control message from the bridge. */
#define JELLI_LOKI_MAX_OUT 512u /* Largest control message the board sends. */
#define JELLI_LOKI_RATE 24000u  /* Sample rate in both directions. */
#define JELLI_LOKI_AUDIO_HEADER 12u
#define JELLI_LOKI_AUDIO_MAX 4800u /* Largest audio payload in bytes. */

/* String buffers, NUL included. */
#define JELLI_LOKI_NAME 41u
#define JELLI_LOKI_TEXT 401u
#define JELLI_LOKI_ID 33u
#define JELLI_LOKI_NONCE 33u
#define JELLI_LOKI_DIGEST 65u
#define JELLI_LOKI_TITLE 121u
#define JELLI_LOKI_DETAIL 601u
#define JELLI_LOKI_OPTION 41u
#define JELLI_LOKI_OPTIONS 4u

typedef enum {
    JELLI_LOKI_UNKNOWN = 0, /* A well-formed message of a type this board does not know. */
    JELLI_LOKI_WELCOME,
    JELLI_LOKI_STATE,
    JELLI_LOKI_CAPTION,
    JELLI_LOKI_ASK,
    JELLI_LOKI_ASK_CANCEL,
    JELLI_LOKI_REPLY_BEGIN,
    JELLI_LOKI_REPLY_END,
    JELLI_LOKI_STOP_AUDIO,
    JELLI_LOKI_SETTINGS
} JelliLokiType;

typedef enum {
    JELLI_LOKI_STATE_IDLE,
    JELLI_LOKI_STATE_LISTENING,
    JELLI_LOKI_STATE_HEARD,
    JELLI_LOKI_STATE_THINKING,
    JELLI_LOKI_STATE_WORKING,
    JELLI_LOKI_STATE_ASKING,
    JELLI_LOKI_STATE_SPEAKING,
    JELLI_LOKI_STATE_SUCCESS,
    JELLI_LOKI_STATE_PARTIAL,
    JELLI_LOKI_STATE_FAILURE
} JelliLokiState;

/* approve: two options, 0 allows and 1 denies. choose: two to four options. */
typedef enum { JELLI_LOKI_APPROVE, JELLI_LOKI_CHOOSE } JelliLokiKind;

/* One decoded message from the bridge. Fields its type lacks stay zero, except
 * progress, which is -1 ("none") unless the message sets it. */
typedef struct {
    JelliLokiType type;
    uint32_t seq;
    uint32_t proto;                                       /* welcome */
    char bridge[JELLI_LOKI_NAME], voice[JELLI_LOKI_NAME]; /* welcome */
    JelliLokiState state;                                 /* state */
    char text[JELLI_LOKI_TEXT];                           /* state caption, caption text */
    int32_t progress;                                     /* state, per mille */
    uint32_t queue;                                       /* state */
    bool final;                                           /* caption */
    char id[JELLI_LOKI_ID], nonce[JELLI_LOKI_NONCE];      /* ask, ask_cancel (id) */
    JelliLokiKind kind;                                   /* ask */
    char title[JELLI_LOKI_TITLE], detail[JELLI_LOKI_DETAIL];
    char options[JELLI_LOKI_OPTIONS][JELLI_LOKI_OPTION];
    unsigned option_count;
    uint32_t timeout_ms;
    uint32_t reply, rate, frames; /* reply_begin, reply_end */
    uint32_t volume;              /* settings */
} JelliLokiMessage;

typedef enum {
    JELLI_LOKI_INVALID, /* Close the connection. */
    JELLI_LOKI_OK,
    JELLI_LOKI_IGNORED /* Unknown type: only type (UNKNOWN) and seq are set. */
} JelliLokiResult;

/* Decodes one control message from the bridge: size bytes, no terminator needed. */
JelliLokiResult jelli_loki_decode(const char *text, size_t size, JelliLokiMessage *message);

/* Writes the canonical form of a decoded message (docs/loki-protocol.md) with a NUL.
 * Returns its length, or 0 when it does not fit (out then holds nothing usable). */
size_t jelli_loki_canonical(const JelliLokiMessage *message, char *out, size_t capacity);

/* The digest an answer carries: lowercase hex SHA-256 of the ask card's canonical
 * "kind=... title=... detail=... options=[...]". */
void jelli_loki_card_digest(const JelliLokiMessage *ask, char hex[JELLI_LOKI_DIGEST]);

typedef enum {
    JELLI_LOKI_NONE = 0, /* A zeroed message: encodes nothing. */
    JELLI_LOKI_HELLO,
    JELLI_LOKI_PTT_START,
    JELLI_LOKI_PTT_END,
    JELLI_LOKI_CANCEL,
    JELLI_LOKI_ANSWER,
    JELLI_LOKI_PLAYED,
    JELLI_LOKI_STATUS
} JelliLokiOutType;

/* One message for the bridge; only the fields of its type are read. */
typedef struct {
    const char *fw, *board;          /* hello */
    const char *id, *nonce, *digest; /* answer */
    JelliLokiOutType type;
    uint32_t seq;
    unsigned channels, frame_ms;      /* hello */
    uint32_t stream, frames, dropped; /* ptt_start, ptt_end */
    unsigned choice;                  /* answer */
    uint32_t reply;                   /* played */
    int battery, rssi;                /* status */
    bool complete;                    /* played */
    bool charging;                    /* status */
} JelliLokiOut;

/* Encodes message as the bridge expects it, with a NUL. Returns the length, or 0
 * when a field breaks the protocol's rules or out is too small. */
size_t jelli_loki_encode(const JelliLokiOut *message, char *out, size_t capacity);

typedef enum { JELLI_LOKI_MIC = 1, JELLI_LOKI_REPLY = 2 } JelliLokiAudioKind;

typedef struct {
    JelliLokiAudioKind kind;
    bool final;      /* The stream's last frame. */
    uint32_t stream; /* ptt_start stream or reply number, never 0. */
    uint32_t seq;    /* 0 for a stream's first frame. */
} JelliLokiAudio;

void jelli_loki_audio_header(const JelliLokiAudio *audio, uint8_t out[JELLI_LOKI_AUDIO_HEADER]);

/* Validates a whole audio frame of the expected kind with channels interleaved;
 * *payload_size gets the length of the samples after the header. */
bool jelli_loki_audio_parse(const uint8_t *frame, size_t size, JelliLokiAudioKind expected,
                            unsigned channels, JelliLokiAudio *audio, size_t *payload_size);

#endif
