NVEICULOS ?= 10

all: controlador cliente veiculo

run: controlador
	NVEICULOS=$(NVEICULOS) ./controlador

controlador: controlador.o
	gcc controlador.o -o controlador -lpthread

cliente: cliente.o
	gcc cliente.o -o cliente -lpthread

veiculo: veiculo.o
	gcc veiculo.o -o veiculo

controlador.o: controlador.c comum.h
	gcc -c controlador.c -o controlador.o

cliente.o: cliente.c comum.h
	gcc -c cliente.c -o cliente.o

veiculo.o: veiculo.c comum.h
	gcc -c veiculo.c -o veiculo.o

clean:
	rm -f *.o controlador cliente veiculo pipe_controlador cliente_*