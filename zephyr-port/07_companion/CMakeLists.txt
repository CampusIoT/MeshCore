# Step 4e — full MeshCore companion (companion_radio MyMesh) on the Zephyr seams.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mc_zephyr_companion)

get_filename_component(MC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CR       "${MC_ROOT}/examples/companion_radio")
set(ALFS     "${MC_ROOT}/arch/stm32/Adafruit_LittleFS_stm32/src")
set(RADIOLIB "$ENV{HOME}/Arduino/libraries/RadioLib/src")
set(CRYPTO   "$ENV{HOME}/Arduino/libraries/Crypto/src")
set(CAYENNE  "$ENV{HOME}/Arduino/libraries/CayenneLPP/src")
set(BASE64   "$ENV{HOME}/Arduino/libraries/base64/src")
set(RTCLIB   "$ENV{HOME}/Arduino/libraries/RTClib/src")
set(AJSON    "$ENV{HOME}/Arduino/libraries/ArduinoJson/src")
set(ED25519  "${MC_ROOT}/lib/ed25519")

# MeshCore core + helper set (mirrors the Arduino companion curation)
file(GLOB MC_CORE ${MC_ROOT}/src/*.cpp)
set(MC_HELPERS
  ${MC_ROOT}/src/helpers/BaseChatMesh.cpp
  ${MC_ROOT}/src/helpers/TransportKeyStore.cpp
  ${MC_ROOT}/src/helpers/IdentityStore.cpp
  ${MC_ROOT}/src/helpers/RegionMap.cpp
  ${MC_ROOT}/src/helpers/ClientACL.cpp
  ${MC_ROOT}/src/helpers/CommonCLI.cpp
  ${MC_ROOT}/src/helpers/AdvertDataHelpers.cpp
  ${MC_ROOT}/src/helpers/TxtDataHelpers.cpp
  ${MC_ROOT}/src/helpers/StaticPoolPacketManager.cpp
  ${MC_ROOT}/src/helpers/radiolib/RadioLibWrappers.cpp)

# companion firmware
set(MC_APP ${CR}/MyMesh.cpp ${CR}/DataStore.cpp)

# third-party (generic/non-Arduino mode for RadioLib)
file(GLOB_RECURSE RL_SOURCES ${RADIOLIB}/*.cpp)
list(FILTER RL_SOURCES EXCLUDE REGEX "/hal/Arduino/")
file(GLOB CRYPTO_SRC ${CRYPTO}/*.cpp ${CRYPTO}/*.c)
file(GLOB BASE64_SRC ${BASE64}/*.cpp)
# CayenneLPP + RTClib are shimmed in compat/ (header-only) — real libs pull STL/I2C.
file(GLOB ED_SRC ${ED25519}/*.c)
set(ALFS_SRC ${ALFS}/Adafruit_LittleFS.cpp ${ALFS}/Adafruit_LittleFS_File.cpp
             ${ALFS}/littlefs/lfs.c ${ALFS}/littlefs/lfs_util.c)

target_sources(app PRIVATE
  src/main.cpp src/target.cpp src/zephyr_internal_fs.cpp src/serial_ble_interface.cpp
  ${MC_CORE} ${MC_HELPERS} ${MC_APP}
  ${RL_SOURCES} ${CRYPTO_SRC} ${BASE64_SRC} ${ED_SRC} ${ALFS_SRC})

target_include_directories(app PRIVATE
  compat src ${MC_ROOT}/src ${CR} ${ALFS}
  ${RADIOLIB} ${CRYPTO} ${BASE64} ${ED25519})

target_compile_definitions(app PRIVATE
  NRF54_PLATFORM=1 RADIOLIB_GODMODE=1 MAX_GROUP_CHANNELS=8 MAX_CONTACTS=100 BLE_PIN_CODE=123456
  LFS_NO_ASSERT   # mount failure -> format instead of abort()
  # app TX-power slider ceiling; the per-band limit is enforced by clamp_tx_for_band() in target.cpp
  MAX_LORA_TX_POWER=22)

# 2.4 GHz band preset (-DMC_BAND_2G4=ON): 2450 MHz / 500 kHz / SF8 / CR5, TX clamped to +12 dBm.
option(MC_BAND_2G4 "Boot on the 2.4 GHz LoRa preset instead of sub-GHz" OFF)
# LoRa boot params are global -D flags (core headers expand LORA_SF in TUs that never see target.h).
if(MC_BAND_2G4)
  target_compile_definitions(app PRIVATE MC_BAND_2G4=1
    LORA_FREQ=2450.0f LORA_BW=500.0f LORA_SF=8 LORA_CR=5 LORA_TX_POWER=12)
  message(STATUS "MeshCore: building for 2.4 GHz band (2450 MHz / 500 kHz / SF8 / CR5)")
else()
  target_compile_definitions(app PRIVATE
    LORA_FREQ=869.618f LORA_BW=62.5f LORA_SF=8 LORA_CR=5 LORA_TX_POWER=22)
endif()

# Multi-SF receive: LR2021 side detectors above the floor SF decode packets across an SF window
# (RX-only; the floor stays the TX default). Defaults: SIDES=3 (SF8 floor -> SF9/10/11).
option(LR2021_MULTISF "Enable LR2021 multi-SF receive (side detectors above the primary SF)" OFF)
if(LR2021_MULTISF)
  target_compile_definitions(app PRIVATE LR2021_MULTISF=1)
  # MESH_MULTISF: the radio-agnostic per-link SF layer (ContactInfo metrics, Packet._tx_sf, SF selection)
  target_compile_definitions(app PRIVATE MESH_MULTISF=1)
  if(LR2021_MULTISF_SIDES)
    target_compile_definitions(app PRIVATE LR2021_MULTISF_SIDES=${LR2021_MULTISF_SIDES})
  endif()
  # experiment knob (default off in code): transmit adverts at floor+offset
  if(DEFINED LR2021_ADVERT_SF_OFFSET)
    target_compile_definitions(app PRIVATE LR2021_ADVERT_SF_OFFSET=${LR2021_ADVERT_SF_OFFSET})
  endif()
  # SIDE_BASE: first side detector at floor+BASE. DETPEAK_ADJ!=0: shift detection thresholds by ADJ.
  if(DEFINED LR2021_MULTISF_SIDE_BASE)
    target_compile_definitions(app PRIVATE LR2021_MULTISF_SIDE_BASE=${LR2021_MULTISF_SIDE_BASE})
  endif()
  if(DEFINED LR2021_MULTISF_DETPEAK_ADJ)
    target_compile_definitions(app PRIVATE LR2021_MULTISF_DETPEAK_ADJ=${LR2021_MULTISF_DETPEAK_ADJ})
  endif()
  # per-packet rx-pkt logging (heavy: printk per RX) -> off by default
  if(LR2021_MULTISF_DEBUG)
    target_compile_definitions(app PRIVATE LR2021_MULTISF_DEBUG=1)
  endif()
  message(STATUS "MeshCore: LR2021 multi-SF receive enabled")
endif()

# Standalone side-detector A/B + diagnostic harness (bypasses the mesh). See ABTEST.md.
#   west build -d build_abtx -- -DMULTISF_ABTEST_ROLE=tx
#   west build -d build_abrx -- -DMULTISF_ABTEST_ROLE=rx   (add -DMULTISF_ABTEST_FIX=ON for the FIX arm)
# Run on the sub-GHz preset (62.5 kHz), not -DMC_BAND_2G4.
if(DEFINED MULTISF_ABTEST_ROLE)
  target_compile_definitions(app PRIVATE MULTISF_ABTEST=1)
  if(MULTISF_ABTEST_ROLE STREQUAL "tx")
    target_compile_definitions(app PRIVATE MULTISF_ABTEST_TX=1)
  endif()
  if(MULTISF_ABTEST_FIX)
    target_compile_definitions(app PRIVATE MULTISF_ABTEST_FIX=1)
  endif()
  if(DEFINED MULTISF_ABTEST_PRIMARY_SF)
    target_compile_definitions(app PRIVATE MULTISF_ABTEST_PRIMARY_SF=${MULTISF_ABTEST_PRIMARY_SF})
  endif()
  message(STATUS "MeshCore: multi-SF A/B test ROLE=${MULTISF_ABTEST_ROLE} FIX=${MULTISF_ABTEST_FIX}")
endif()
