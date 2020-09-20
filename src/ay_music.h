
int ay_music_init(void);

void ay_music_play(void);

void ay_music_stop(void);

void ay_music_select(int num);

/** После первого вызова придётся вызывать периодически, иначе воспроизведение приостановится. */
void ay_music_continue(int t);
