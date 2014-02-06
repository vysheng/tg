# Copyright 1999-2013 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI=5

EGIT_REPO_URI="https://github.com/vysheng/tg.git"
inherit git-2
IUSE="lua"
DESCRIPTION="Command line interface client for Telegram"
HOMEPAGE="https://github.com/vysheng/tg"
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="~amd64 ~x86"

DEPEND="sys-libs/zlib
	sys-libs/readline
	dev-libs/libconfig
	dev-libs/openssl
	lua? ( dev-lang/lua )"

src_configure() {
	econf $(use_enable lua liblua ) --with-progname=telegram-cli
}

src_install() {
	newbin telegram telegram-cli

	insinto /etc/telegram-cli/
	newins tg.pub server.pub
}
