import React, {useId} from "react"
import withJuceSlider, {WithJUCESliderProps} from "./withJuceSlider"
import SliderControl from "react-slider"
import functions from "../structures/Functions"
import "./styles/slider.scss"

interface Props {
    parameterID: string
    label: string
    color: string
    style?: React.CSSProperties
}

const Slider: React.FunctionComponent<Props & WithJUCESliderProps> = ({label, parameterID, 
    color, style, value, properties, onChange, reset, dragStart, dragEnd}) => {

    const handleKeyDown = (event: React.KeyboardEvent) => {
        if (event.key === "ArrowUp") {
            event.preventDefault()
            onChange(Math.min(24, Math.round(value) + 1))
        }

        if (event.key === "ArrowDown") {
            event.preventDefault()
            onChange(Math.max(-24, Math.round(value) - 1))
        }
    }

    return (
        <div className="slider-container" style={{...style}}>
            <div className="slider-label">
                {label}
            </div>
            <div style={{flex: 1}} 
                onDoubleClick={reset}
                onKeyDown={handleKeyDown}>
            <SliderControl 
                orientation="vertical"
                invert
                className="pitch-slider" 
                trackClassName="pitch-slider-track" 
                thumbClassName="pitch-slider-thumb" 
                onChange={onChange} 
                min={-24} 
                max={24}
                step={1}
                value={value}
                onDragStart={dragStart}
                onDragEnd={dragEnd}/>
            </div>
            <div className="slider-value" style={{color}}>
                {Math.round(value)} st
            </div>
        </div>
    )
}

export default withJuceSlider(Slider)