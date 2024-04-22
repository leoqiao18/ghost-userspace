#!/usr/bin/env python3

import os
import tempfile

temp = tempfile.TemporaryFile()

while True:
    temp.write(b'hello world')
    os.sync()
    temp.seek(0)
