# Protocol sources and evaluated libraries

The navigation/driver implementation in this directory is RooCore code. No third-party SLAM implementation is vendored or represented as validated on the XIAO.

- iRobot Create 2 / Roomba 600 OI specification, 2021-04-13: https://cdn.hackaday.io/files/1835247851890816/2021-04-13_iRobot_Roomba_600_Open_Interface_Spec-3.pdf . Safe-mode intervention, sensors, encoder/firmware caveats and Passive charging behavior also documented in `docs/OPEN_INTERFACE.md`.
- YDLIDAR SDK revision `e008f32ed7477c6351b49e2215f8fc5a797b8959`: `doc/Dataset.md` specifies model 151, 230400, intensity 8; `examples/tmini_test.cpp` selects TYPE_TRIANGLE and 8-bit intensity; `src/YDlidarDriver.cpp` verifies header/sample XOR and removes low two distance flag bits, without second-order angle correction for T-mini; `src/CYdLidar.cpp` converts triangle protocol distance units by /4000 to metres. https://github.com/YDLIDAR/YDLidar-SDK/tree/e008f32ed7477c6351b49e2215f8fc5a797b8959
- YDLIDAR model 152 SH uses a different protocol/intensity configuration; the firmware intentionally does not auto-guess formats.
- Seeed board reference: https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/ . Plain S3, 8 MB flash / 8 MB PSRAM; no camera dependency.
- Nav2: https://docs.nav2.org/jazzy/getting_started/build_and_install/local_installation/ and SLAM Toolbox: https://github.com/SteveMacenski/slam_toolbox . Selected as optional Linux ROS 2 navigation, not installed on the microcontroller.
- BreezySLAM/CoreSLAM: https://github.com/simondlevy/BreezySLAM/tree/1170285cd42a88300eddd275fc3df36e642e3a2e . Evaluated as a possible lighter mapper; no XIAO performance or integration validation, and not used by this release. The onboard fallback is explicitly a limited local mapper.
- Arduino LDS revision `6e17803760908b3898d3b7f3f912e8c4f8b7bafd`: https://github.com/kaiaai/LDS/tree/6e17803760908b3898d3b7f3f912e8c4f8b7bafd . T-mini support is labelled experimental. Its `LDS_YDLIDAR_TMINI.cpp` reads two bytes/sample; the manufacturer's model 151 configuration includes an intensity byte plus two distance bytes. This mismatch is why it was not adopted unchanged.
