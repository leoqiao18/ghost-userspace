import hashlib
import random
import string
import sys

start = int(sys.argv[1])

def crypto():
    for _ in range(6900):
        # Generate a random string of 1000 characters
        current_time = "".join(random.choices(string.ascii_letters + string.digits, k=1000))
        # Compute the cryptographic function (SHA-256) for the current time
        hash_value = hashlib.sha256(current_time.encode()).hexdigest()

def mem():
    data = {}

    for _ in range(18):
        # Generate a large chunk of data (e.g., 100 MB)
        chunk_size = 100 * 1024 * 1024  # 100 MB
        chunk = bytearray(chunk_size)

        # Write the chunk of data to the dictionary
        data["chunk"] = chunk

        # Read the chunk of data from the dictionary
        read_chunk = data["chunk"]

        # Perform any benchmarking operations on the read_chunk if needed

        # Clear the dictionary to free up memory
        data.clear()

if start == 0:
    mem()

while True:
    crypto()
    mem()
