import os

file_size = 100 * 1024 * 1024  # 100MB in bytes

with open('test-files/100mb.txt', 'wb') as f:
    f.write(os.urandom(file_size))