# Maintainer: Onni R. <onnir at iki dot fi>
pkgname=esjit
pkgver=1
pkgrel=1
pkgdesc="text-mode interface for handling JACK connections"
arch=('i686' 'x86_64')
url="http://github.com/lotuskip/esjit"
depends=('jack')
source=(http://tempoanon.net/lotuskip/tervat/$pkgname-$pkgver.tar.gz)
md5sums=('c7039af2d826786472409af82cd96c7d')

build() {
  mkdir -p "${pkgdir}/usr/bin" || return 1
  cd $srcdir
  g++ -O2 -o $pkgdir/usr/bin/esjit esjit.cpp -ljack || return 1    
}
