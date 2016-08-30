# AVR-GCC Makefile
PROJECT=noise-generator

#space separated list of source files
SOURCES=main.c
CC=avr-gcc
OBJCOPY=avr-objcopy
MMCU=attiny44a

CFLAGS=-mmcu=$(MMCU) -Wall -Os

$(PROJECT).hex: $(PROJECT).out
	$(OBJCOPY) -j .text -O ihex $(PROJECT).out $(PROJECT).hex

$(PROJECT).out: $(SOURCES) 
	$(CC) $(CFLAGS) -I./ -o $(PROJECT).out $(SOURCES)

program: $(PROJECT).hex
	avrdude -c usbtiny -e -p t44 -U flash:w:$(PROJECT).hex
clean:
	rm -f $(PROJECT).o
	rm -f $(PROJECT).out
	rm -f $(PROJECT).hex
