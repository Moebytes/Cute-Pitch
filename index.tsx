import "bootstrap/dist/css/bootstrap.min.css"
import React, {useState, useEffect} from "react"
import {createRoot} from "react-dom/client"
import Slider from "./components/Slider"
import LFOBar from "./components/LFOBar"
import PresetBar from "./components/PresetBar"
import DarkIcon from "./assets/dark.svg"
import LightIcon from "./assets/light.svg"
import parameters from "./processor/parameters.json"
import functions from "./structures/Functions"
import "./index.scss"

const darkColorList = {
    "--background": "#220E1A",
    "--textColor": "#FFFFFF"
}

const lightColorList = {
    "--background": "#FFBEE6",
    "--textColor": "#000000",
}

type ThemeContextType = {theme: string; setTheme: React.Dispatch<React.SetStateAction<string>>}
export const ThemeContext = React.createContext<ThemeContextType>({theme: "", setTheme: () => null})

const App: React.FunctionComponent = () => {
    const [theme, setTheme] = useState(localStorage.getItem("theme") || "light")
    
    useEffect(() => {
        window.__JUCE__.backend.emitEvent("themeChange", {theme})
    }, [])

    useEffect(() => {
        const colorList = theme === "light" ? lightColorList : darkColorList
        for (const [key, color] of Object.entries(colorList)) {
            document.documentElement.style.setProperty(key, color)
        }
        localStorage.setItem("theme", theme)
        window.__JUCE__.backend.emitEvent("themeChange", {theme})
    }, [theme])

    const toggleTheme = () => {
        setTheme((prev) => prev === "light" ? "dark" : "light")
    }

    const filter = functions.calculateFilter("#FF4EA7")

    return (
        <div className="app">
            <div className="title-container">
                <span className="title-text">Cute Pitch</span>
                {theme === "light" ? 
                <DarkIcon className="theme-icon" style={{filter}} onClick={toggleTheme}/> :
                <LightIcon className="theme-icon" style={{filter}} onClick={toggleTheme}/>}
            </div>
            <div className="sliders-container">
                <Slider 
                    label={parameters.pitch.id.toUpperCase()} 
                    parameterID={parameters.pitch.id} 
                    color="#FF4EA7"/>
                <Slider 
                    label={parameters.formant.id.toUpperCase()} 
                    parameterID={parameters.formant.id} 
                    color="#FF4EA7"/>
            </div>
            <div className="lfo-container">
                <LFOBar 
                    label={parameters.pitch.id.toUpperCase()}
                    lfoTypeID={parameters.pitchLFOType.id} 
                    lfoRateID={parameters.pitchLFORate.id} 
                    lfoAmountID={parameters.pitchLFOAmount.id} 
                    lfoInvertID={parameters.pitchLFOInvert.id}
                    color="#FF4EA7"
                    theme={theme}/>
            </div>
            <div className="preset-container">
                <PresetBar/>
            </div>
        </div>
    )

}

const root = createRoot(document.getElementById("root")!)
root.render(<App/>)