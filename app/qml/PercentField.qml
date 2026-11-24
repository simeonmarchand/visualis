import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A fraction of the output, shown and typed as a percentage in tenths of a percent.
SpinBox {
    id: field
    required property real fraction
    signal fractionEdited(real fraction)
    from: 0
    to: 1000
    stepSize: 5
    editable: true
    value: Math.round(field.fraction * 1000)
    textFromValue: (v, locale) => (v / 10).toFixed(1) + "%"
    valueFromText: (text, locale) => Math.round(parseFloat(text) * 10)
    onValueModified: fractionEdited(value / 1000)
    Layout.preferredWidth: 110
}
