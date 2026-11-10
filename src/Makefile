CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lpng -ltiff -ljpeg -lgif -lwebp -lm

TARGET = descolorir
SRC = descolorir.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
