# Get some needed packages:



# Download sphinxbase:
wget https://sourceforge.net/projects/cmusphinx/files/sphinxbase/5prealpha/sphinxbase-5prealpha.tar.gz
gunzip sphinxbase-5prealpha.tar.gz
tar -xvf sphinxbase-5prealpha.tar
mv sphinxbase-5prealpha sphinxbase

# Build sphinxbase:
cd sphinxbase
./autogen.sh
./configure
make
sudo make install
cd -

export LD_LIBRARY_PATH=/usr/local/lib
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig

# Download  pocketsphinx:
wget https://sourceforge.net/projects/cmusphinx/files/pocketsphinx/5prealpha/pocketsphinx-5prealpha.tar.gz
gunzip pocketsphinx-5prealpha.tar.gz
tar -xvf pocketsphinx-5prealpha.tar

# Build  pocketsphinx:
cd pocketsphinx-5prealpha
./autogen.sh
./configure
make
sudo make install
cd -

echo "####### Done Installation ################"
echo "To test the installation, run:"
echo "   pocketsphinx_continuous -inmic yes -adcdev plughw:M,N"
echo " and check that it recognizes words you speak into your microphone."
