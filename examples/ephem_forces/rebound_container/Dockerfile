FROM ubuntu:18.04
MAINTAINER Matt Payne @ MPC <matthewjohnpayne@gmail.com>


# --- SYSTEM INSTALLS -----------
RUN apt-get -y update && apt-get install -y 	gcc  		\
    						make 		\ 
    						git		\ 	
						python3.8 	\
						python3-pip	\
						perl		\
						wget 		\
						bzip2		\
						vim		\
						sudo		

# --- PYTHON INSTALLS -----------
# Anaconda installing 
#RUN wget https://repo.continuum.io/archive/Anaconda3-5.0.1-Linux-x86_64.sh
#RUN bash Anaconda3-5.0.1-Linux-x86_64.sh -b
#RUN rm Anaconda3-5.0.1-Linux-x86_64.sh
RUN python3.8 -m pip install numpy


# REBOUND INSTALLS ----------
RUN git clone https://github.com/hannorein/rebound.git
WORKDIR /rebound
RUN make
ENV REB_DIR=/rebound
ENV LD_LIBRARY_PATH=/rebound/src

WORKDIR /
RUN git clone -b mpc_dev https://github.com/matthewjohnpayne/reboundx.git
WORKDIR /reboundx/examples/ephem_forces/
RUN perl -pi -e 's/OPENGL=1/OPENGL=0/g' Makefile
RUN make
ENV REBX_DIR=/reboundx
ENV LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/reboundx/src

RUN wget ftp://ssd.jpl.nasa.gov/pub/eph/small_bodies/asteroids_de441/sb441-n16.bsp




# --- Run deployment code on entry :
COPY pythonServer.py /pythonServer.py
ENTRYPOINT ["python3.8", "/pythonServer.py"]




