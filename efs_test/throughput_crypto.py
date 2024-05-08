import hashlib
import random
import string

# while True:
for _ in range(70000):
    # Generate a random string of 1000 characters
    current_time = "".join(random.choices(string.ascii_letters + string.digits, k=1000))
    # Compute the cryptographic function (SHA-256) for the current time
    hash_value = hashlib.sha256(current_time.encode()).hexdigest()
