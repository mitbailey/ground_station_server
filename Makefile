CXX = g++
COBJS = src/main.o src/gss.o network/network.o
CXXFLAGS = -I ./include/ -I ./network/ -Wall -pthread
TARGET = server.out

all: $(COBJS)
	$(CXX) $(CXXFLAGS) $(COBJS) -o $(TARGET)
	./$(TARGET)

%.o: %.c
	$(CXX) $(CXXFLAGS) -o $@ -c $<

.PHONY: clean

clean:
	$(RM) *.out
	$(RM) *.o
	$(RM) src/*.o