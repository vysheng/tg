EAPI=6

inherit git-r3

HOMEPAGE="https://github.com/vysheng/tg"
DESCRIPTION="Command line interface client for Telegram"
EGIT_REPO_URI="https://github.com/vysheng/tg.git"
if [[ "${PV}" -ne "9999" ]]; then
	EGIT_COMMIT="refs/tags/${PV}"
	KEYWORDS="~amd64 ~x86"
else
	KEYWORDS=""
fi
LICENSE="GPL-2"
SLOT="0"
IUSE="lua json +python"

DEPEND="
	sys-libs/zlib
	sys-libs/readline
	dev-libs/libconfig
	dev-libs/openssl
	dev-libs/libevent
	lua? ( dev-lang/lua )
	json? ( dev-libs/jansson )
	python? ( dev-lang/python )
	"
RDEPEND="${DEPEND}"

src_configure() {
	econf $(use_enable lua liblua) \
	      $(use_enable python) \
	      $(use_enable json)
}

src_install() {
	dobin bin/telegram-cli

	insinto /etc/telegram-cli/
	newins tg-server.pub server.pub
}
