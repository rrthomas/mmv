import re
import sys

from . import main
sys.argv[0] = re.sub(r'__main__.py$', 'mmv', sys.argv[0])
main()
