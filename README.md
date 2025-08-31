About : Distributed Key-Value Store with CLI & GUI client.

Features till now :
  1. Multiple clients support on a single server.
  2. Uses a NetCat connection to listen to the server (CLI client not implemented yet).
  3. GUI Client is not implemented yet.
  4. Simple functionalities, no replication, sharding, TTL, security fatures, multiple database/namespace support or scalable features available right now.
  5. Available Commands :
     a. PUT "key" "value" -> Creates a new key = "key" with value = "value".
     b. GET "key" -> Returns the value stored with key = "key".
     c. GET_KEY "value" -> Returns the "key" with value = "value".
     d. UPDATE "key" "new_value" -> Updates the "old_value" stored at "key" with "new_value".
     e. DELETE "key" -> Deletes the key = "key" (therefore its value).
     f. SHUTDOWN -> Gracefully shuts down the server.
