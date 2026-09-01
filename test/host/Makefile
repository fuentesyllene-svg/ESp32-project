# Host-side tests for the algorithmic firmware sources.
#   make        build and run
#   make clean
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter

MAIN_SRC = ../../arduino/PestTrapController
CAM_SRC  = ../../arduino/PestTrapCamera

INCLUDES = -Ishim -I$(MAIN_SRC) -I$(CAM_SRC)

SOURCES = test_main.cpp \
          actuators_stub.cpp \
          shim/shim.cpp \
          shim/esp_camera.cpp \
          $(MAIN_SRC)/power.cpp \
          $(MAIN_SRC)/trap.cpp \
          $(MAIN_SRC)/cam_link.cpp \
          $(CAM_SRC)/detector.cpp

all: run

host_tests: $(SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(SOURCES) -lm

run: host_tests
	./host_tests

clean:
	rm -f host_tests

.PHONY: all run clean
