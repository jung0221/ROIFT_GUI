#include "MaskOptionsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>

MaskOptionsDialog::MaskOptionsDialog(QWidget *parent): QDialog(parent) {
    setWindowTitle("Mask Options");
    QVBoxLayout *v = new QVBoxLayout(this);
    QHBoxLayout *tog = new QHBoxLayout();
    m_btnDraw = new QPushButton("Draw"); m_btnDraw->setCheckable(true);
    m_btnErase = new QPushButton("Erase"); m_btnErase->setCheckable(true);
    tog->addWidget(m_btnDraw); tog->addWidget(m_btnErase);
    v->addLayout(tog);

    QHBoxLayout *brushh = new QHBoxLayout();
    brushh->addWidget(new QLabel("Mask brush:"));
    m_brushRadius = new QSpinBox(); m_brushRadius->setRange(1,200); m_brushRadius->setValue(6);
    brushh->addWidget(m_brushRadius);
    v->addLayout(brushh);

    QHBoxLayout *opacH = new QHBoxLayout();
    opacH->addWidget(new QLabel("Mask opacity:"));
    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(50); // default 50% -> 0.5
    opacH->addWidget(m_opacitySlider);
    v->addLayout(opacH);

    QHBoxLayout *fileh = new QHBoxLayout();
    QPushButton *btnLoad = new QPushButton("Load Mask");
    QPushButton *btnSave = new QPushButton("Save Mask");
    QPushButton *btnClean = new QPushButton("Clean Mask");
    fileh->addWidget(btnLoad); fileh->addWidget(btnSave); fileh->addWidget(btnClean);
    v->addLayout(fileh);

    connect(m_btnDraw, &QPushButton::toggled, this, [this](bool checked){ if(checked){ m_btnErase->setChecked(false); m_mode=1; emit modeChanged(1);} else { if(!m_btnErase->isChecked()){ m_mode=0; emit modeChanged(0);} } });
    connect(m_btnErase, &QPushButton::toggled, this, [this](bool checked){ if(checked){ m_btnDraw->setChecked(false); m_mode=2; emit modeChanged(2);} else { if(!m_btnDraw->isChecked()){ m_mode=0; emit modeChanged(0);} } });
    connect(btnClean, &QPushButton::clicked, this, [this](){ emit cleanRequested(); });
    connect(btnLoad, &QPushButton::clicked, this, [this](){ emit loadMaskRequested(); });
    connect(btnSave, &QPushButton::clicked, this, [this](){ emit saveMaskRequested(); });
    connect(m_brushRadius, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v){ emit brushRadiusChanged(v); });
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int v){ emit maskOpacityChanged(v); });

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Close);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
