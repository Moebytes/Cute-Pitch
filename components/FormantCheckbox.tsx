import React, {useState, useRef, useEffect, useContext} from "react"
import withJuceToggleButton, {WithJUCEToggleButtonProps} from "./withJuceToggleButton"
import "./styles/formantcheckbox.scss"

interface FormantCheckboxProps {
    parameterID: string
}

const FormantCheckbox: React.FunctionComponent<FormantCheckboxProps & WithJUCEToggleButtonProps> = ({value, onChange}) => {
    return (
        <div className="formant-checkbox-container">
            <span className="formant-checkbox-text">Preserve Formant?</span>
            <div className={`formant-checkbox ${value ? "checked" : ""}`} onClick={() => onChange(!value)}></div>
        </div>
    )
}

export default withJuceToggleButton(FormantCheckbox)