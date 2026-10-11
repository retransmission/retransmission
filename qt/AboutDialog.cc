// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "AboutDialog.h"

#include <QtCore/QUrl>

#include <QtGui/QDesktopServices>
#include <QtGui/QIcon>

#include <QtWidgets/QApplication>
#include <QtWidgets/QPushButton>

#include <libtransmission/macros.h>
#include <libtransmission/transmission.h>
#include <libtransmission/version.h>

#include "Session.h"
#include "TrFormat.h"
#include "Utils.h"

AboutDialog::AboutDialog(Session& session, QWidget* parent)
    : BaseDialog{ parent }
{
    ui_.setupUi(this);
    setWindowTitle(Utils::withAppName(windowTitle()));
    ui_.copyrightsLabel->setText(
        TR_FORMAT("Copyright © The {appname} Project", fmt::arg("appname", TR_PROJ_APPNAME_CAPITALIZED)));

    ui_.iconLabel->setPixmap(QApplication::windowIcon().pixmap(48));

    if (session.is_embedded()) {
        auto const title = QStringLiteral("<b style='font-size:x-large'>" TR_PROJ_APPNAME_CAPITALIZED " %1</b>")
                               .arg(QStringLiteral(LONG_VERSION_STRING));
        ui_.titleLabel->setText(title);
    } else {
        QString title = QStringLiteral(
            "<div style='font-size:x-large; font-weight: bold; text-align: center'>" TR_PROJ_APPNAME_CAPITALIZED "</div>");
        title += QStringLiteral("<div style='text-align: center'>%1: %2</div>")
                     .arg(tr("Client"), QStringLiteral(LONG_VERSION_STRING));
        title += QStringLiteral("<div style='text-align: center'>%1: %2</div>").arg(tr("Server"), session.sessionVersion());
        ui_.titleLabel->setText(title);
    }

    QPushButton const* b = ui_.dialogButtons->addButton(tr("C&redits"), QDialogButtonBox::ActionRole);
    connect(b, &QAbstractButton::clicked, this, [] { QDesktopServices::openUrl(QUrl{ QStringLiteral(TR_PROJ_URL_CREDITS) }); });

    ui_.dialogButtons->button(QDialogButtonBox::Close)->setDefault(true);
}
