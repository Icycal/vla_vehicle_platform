uint8 STATE_DISCONNECTED=0
uint8 STATE_LOADING=1
uint8 STATE_READY=2
uint8 STATE_ERROR=3

std_msgs/Header header
uint8 state
string provider_id
string model_id
string protocol_version
string observation_schema
string action_schema
float32 inference_latency_ms
string message
