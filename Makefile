# ============================================================
# NeuralOS X — Phase 3 Makefile
# CL-2006 Operating Systems Lab | Spring 2026
# ============================================================

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic \
            -I./include -pthread -g
LDFLAGS  := -pthread

TARGET   := NeuralOS_X
SRC      := src/main.cpp
BINDIR   := bin
LOGDIR   := logs

.PHONY: all clean run dirs

all: dirs $(TARGET)

dirs:
	@mkdir -p $(BINDIR) $(LOGDIR)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)
	@echo ""
	@echo "Build successful: ./$(TARGET)"
	@echo "Usage: ./$(TARGET) <RAM_GB> <HDD_GB> <CORES>"

run: all
	./$(TARGET) 1 10 2

clean:
	rm -f $(TARGET) $(LOGDIR)/neuralOS_log.txt
