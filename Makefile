all: build

build:
	pio run
upload:
	pio run -t upload
clean:
	pio run -t clean
