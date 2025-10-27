#include "SeedOptionsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>

SeedOptionsDialog::SeedOptionsDialog(QWidget *parent): QDialog(parent) {
    setWindowTitle("Seed Options");
    QVBoxLayout *v = new QVBoxLayout(this);
    QHBoxLayout *tog = new QHBoxLayout();
    m_btnDraw = new QPushButton("Draw"); m_btnDraw->setCheckable(true);
    m_btnErase = new QPushButton("Erase"); m_btnErase->setCheckable(true);
    m_btnDraw->setChecked(true);
    tog->addWidget(m_btnDraw); tog->addWidget(m_btnErase);
    v->addLayout(tog);

    QHBoxLayout *brushh = new QHBoxLayout();
    brushh->addWidget(new QLabel("Seed brush:"));
    m_brushRadius = new QSpinBox(); m_brushRadius->setRange(1,200); m_brushRadius->setValue(5);
    brushh->addWidget(m_brushRadius);
    v->addLayout(brushh);

    QHBoxLayout *fileh = new QHBoxLayout();
    QPushButton *btnSave = new QPushButton("Save Seeds");
    QPushButton *btnLoad = new QPushButton("Load Seeds");
    QPushButton *btnClear = new QPushButton("Clear Seeds");
    fileh->addWidget(btnSave); fileh->addWidget(btnLoad); fileh->addWidget(btnClear);
    v->addLayout(fileh);

    connect(m_btnDraw, &QPushButton::toggled, this, [this](bool checked){ if(checked){ m_btnErase->setChecked(false); m_mode=1; emit modeChanged(1);} else { if(!m_btnErase->isChecked()){ m_mode=0; emit modeChanged(0);} } });
    connect(m_btnErase, &QPushButton::toggled, this, [this](bool checked){ if(checked){ m_btnDraw->setChecked(false); m_mode=2; emit modeChanged(2);} else { if(!m_btnDraw->isChecked()){ m_mode=0; emit modeChanged(0);} } });
    connect(btnClear, &QPushButton::clicked, this, [this](){ emit cleared(); });
    connect(btnSave, &QPushButton::clicked, this, [this](){ emit saveRequested(); });
    connect(btnLoad, &QPushButton::clicked, this, [this](){ emit loadRequested(); });
    connect(m_brushRadius, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v){ emit brushRadiusChanged(v); });

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Close);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
