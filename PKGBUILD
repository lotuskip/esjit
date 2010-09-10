# Maintainer: Onni R. <onnir at iki dot fi>
pkgname=esjit
pkgver=1
pkgrel=1
pkgdesc="text-mode interface for handling JACK connections"
arch=('i686' 'x86_64')
url="http://github.com/lotuskip/esjit"
depends=('jack' 'boost')
source=(http://tempoanon.net/lotuskip/tervat/$pkgname-$pkgver.tar.gz)
md5sums=('5aca1bc597810a0b1f9ca8a21d272e3e')

build() {
  mkdir -p "${pkgdir}/usr/bin" || return 1
  cd $srcdir
  g++ -O2 -o $pkgdir/usr/bin/esjit esjit.cpp -ljack || return 1    
}
