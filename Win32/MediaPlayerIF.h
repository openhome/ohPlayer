// Media Player Control IF.

                                         // Initialise the media player thread.
DWORD WINAPI InitAndRunMediaPlayer( LPVOID lpParam );
void ExitMediaPlayer();                  // Terminate the media player thread.
void PipeLinePlay();                     // Pipeline - Play
void PipeLinePause();                    // Pipeline - Pause
void PipeLineStop();                     // Pipeline - Stop
