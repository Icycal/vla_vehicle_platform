std_msgs/Header header
string request_id
string observation_id
string model_id
string schema_version
string schema_hash
string[] feature_names
string[] feature_units
builtin_interfaces/Time generated_at
builtin_interfaces/Time valid_until
builtin_interfaces/Duration control_period
vehicle_interfaces/ActionVector[] action_vectors
# Deprecated migration field for vehicle.twist_chunk.v1 consumers.
geometry_msgs/Twist[] actions
