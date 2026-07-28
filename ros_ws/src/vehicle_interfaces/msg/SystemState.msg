uint8 MODE_MANUAL=0
uint8 MODE_NAV2=1
uint8 MODE_VLA_SHADOW=2
uint8 MODE_VLA_ASSISTED=3
uint8 MODE_VLA_AUTONOMOUS=4
uint8 MODE_MOBILE_TELEOP=5
uint8 MODE_REMOTE_TELEOP=6
uint8 MODE_SAFE_STOP=7
uint8 MODE_FAULT=8

std_msgs/Header header
uint8 mode
string control_source
string active_mission_id
string active_policy_id
bool safe_to_move
string status_message
