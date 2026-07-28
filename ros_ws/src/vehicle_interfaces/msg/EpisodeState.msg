uint8 STATE_IDLE=0
uint8 STATE_RECORDING=1
uint8 STATE_FINALIZING=2
uint8 STATE_ERROR=3

std_msgs/Header header
uint8 state
string episode_id
string task
string directory
uint64 message_count
uint64 image_count
float32 elapsed_seconds
string message
