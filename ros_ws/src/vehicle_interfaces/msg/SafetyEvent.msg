uint8 SEVERITY_INFO=0
uint8 SEVERITY_WARNING=1
uint8 SEVERITY_STOP=2
uint8 SEVERITY_FAULT=3

std_msgs/Header header
uint8 severity
string rule_id
string reason
bool active
