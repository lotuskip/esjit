# Maintainer: Onni R. <onnir at iki dot fi>
pkgname=esjit
pkgver=1
pkgrel=1
#license=() is not missing; THERE IS NO LICENSE!
pkgdesc="a text-mode interface for handling JACK connections"
arch=('i686' 'x86_64')
url="http://github.com/lotuskip/esjit"
makedepends=('boost')
depends=('jack' 'boost-libs')
source=(http://tempoanon.net/lotuskip/tervat/$pkgname-$pkgver.tar.gz)
md5sums=('84f71967b568e8bb7ca112c52bfed55b')

build() {
  mkdir -p "${pkgdir}/usr/bin" || return 1
  cd $srcdir
  g++ -O2 -o $pkgdir/usr/bin/esjit esjit.cpp -ljack || return 1
  install -Dm644 esjit.1 $pkgdir/usr/share/man/man1/esjit.1
}

