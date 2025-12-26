# Variables
SSL_LIBS=
SSL_FLAGS=

# Regla principal
all: soapC.c client server

# Generar soapC
soapC.c:
	soapcpp2 -b -c blackJack.h

# Compilar cliente
client:
	gcc $(SSL_FLAGS) -w -o client client.c soapC.c soapClient.c game.c -lgsoap $(SSL_LIBS) -L$(GSOAP_LIB) -I$(GSOAP_INCLUDE)

# Compilar servidor
server: 
	gcc $(SSL_FLAGS) -w -o server server.c soapC.c soapServer.c game.c -lgsoap $(SSL_LIBS) -L$(GSOAP_LIB) -I$(GSOAP_INCLUDE) 

# Limpieza
clean:  
	rm -f client server *.xml *.nsmap *.wsdl *.xsd soapStub.h soapServerLib.* soapH.h soapServer.* soapClientLib.* soapClient.* soapC.*