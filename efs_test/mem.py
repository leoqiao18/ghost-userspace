data = {}

while True:
# for _ in range(18):
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
