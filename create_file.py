import os

file_size = 30 * 1024 * 1024 * 1024  # 30GB in bytes

os.makedirs('test-files', exist_ok=True)
with open('test-files/test.txt', 'wb') as f:
    f.write(os.urandom(file_size))
