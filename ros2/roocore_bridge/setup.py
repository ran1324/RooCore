from setuptools import setup
from glob import glob
setup(name='roocore_bridge', version='0.1.0', packages=['roocore_bridge'],
      data_files=[('share/ament_index/resource_index/packages', ['resource/roocore_bridge']),
                  ('share/roocore_bridge', ['package.xml']),
                  ('share/roocore_bridge/launch', glob('launch/*.launch.py')),
                  ('share/roocore_bridge/config', glob('config/*'))],
      install_requires=['setuptools'], zip_safe=True,
      entry_points={'console_scripts': ['bridge = roocore_bridge.bridge:main',
                                       'cleaner = roocore_bridge.cleaner:main']})
