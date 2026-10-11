CC      := cc
CFLAGS  := -O2 -Wall -Wextra
TARGET  := fx100-pedal
SRC     := fx100-pedal.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
