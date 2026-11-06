/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import React, { Component, ReactNode } from "react";

import {
    Chart as ChartJS,
    LinearScale,
    PointElement,
    LineElement,
    Filler,
    ChartOptions
} from "chart.js";

import { Line } from "react-chartjs-2";

import "HistogramView.css";

import { Vector2D } from "./Vector";

interface HistogramViewProps {
    width: number;
    height: number;
}

class HistogramView extends Component<HistogramViewProps> {
    private readonly HISTOGRAM_SAMPLES_PER_CHANNEL = 256;

    private _chartRef: React.RefObject<ChartJS<"line">>;
    private _chartOptions: ChartOptions<"line">;
    private _chartData;

    constructor(props: HistogramViewProps) {
        super(props);

        // Create ChartJS Chart Reference to display the histogram
        ChartJS.register(LinearScale, PointElement, LineElement, Filler);

        this._chartRef = React.createRef();

        this._chartOptions = {
            animation: false,
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: {
                    type: "linear",
                    min: 0.0,
                    max: this.HISTOGRAM_SAMPLES_PER_CHANNEL,
                    ticks: { display: false },
                    grid: { display: true },
                    border: { display: true }
                },
                y: {
                    type: "linear",
                    beginAtZero: true,
                    ticks: { display: false },
                    grid: { display: true },
                    border: { display: true }
                }
            }
        };

        //
        const channels: Vector2D[][] = Array(3);

        channels[0] = new Array<Vector2D>(this.HISTOGRAM_SAMPLES_PER_CHANNEL)
            .fill({ x: 0.0, y: 0.0 })
            .map((value, index) => ({ x: index, y: value.y }));
        channels[1] = new Array<Vector2D>(this.HISTOGRAM_SAMPLES_PER_CHANNEL)
            .fill({ x: 0.0, y: 0.0 })
            .map((value, index) => ({ x: index, y: value.y }));
        channels[2] = new Array<Vector2D>(this.HISTOGRAM_SAMPLES_PER_CHANNEL)
            .fill({ x: 0.0, y: 0.0 })
            .map((value, index) => ({ x: index, y: value.y }));

        // Datasets ordered to match the AXI4-Stream Video GBR channel layout
        // produced by the CalcHist core. dataset[0] = Green, [1] = Blue, [2] = Red.
        this._chartData = {
            datasets: [
                {
                    label: "Green",
                    data: channels[0],
                    pointRadius: 0,
                    borderWidth: 1,
                    backgroundColor: "#44BD8477",
                    borderColor: "#44BD84",
                    fill: "origin"
                },
                {
                    label: "Blue",
                    data: channels[1],
                    pointRadius: 0,
                    borderWidth: 1,
                    backgroundColor: "#3B62DA77",
                    borderColor: "#3B62DA",
                    fill: "origin"
                },
                {
                    label: "Red",
                    data: channels[2],
                    pointRadius: 0,
                    borderWidth: 1,
                    backgroundColor: "#EB575777",
                    borderColor: "#EB5757",
                    fill: "origin"
                }
            ]
        };
    }

    render(): ReactNode {
        return (
            <div className="okHistogramChartPanel">
                <div className="okHistogramChartContainer">
                    <Line ref={this._chartRef} options={this._chartOptions} data={this._chartData} />
                </div>
            </div>
        );
    }

    /**
     * Update the chart with a new histogram buffer captured from the pipeline.
     *
     * Dataset and buffer are both in GBR order (channel 0 = Green, 1 = Blue,
     * 2 = Red), so dataset index and channel index are the same thing.
     */
    public updateHistogram(histogram: Uint32Array): void {
        if (this._chartRef.current == null) return;

        for (let ch = 0; ch < 3; ch++) {
            const samples = histogram.subarray(
                ch * this.HISTOGRAM_SAMPLES_PER_CHANNEL,
                (ch + 1) * this.HISTOGRAM_SAMPLES_PER_CHANNEL
            );
            const data = this._chartData.datasets[ch].data;
            for (let i = 0; i < this.HISTOGRAM_SAMPLES_PER_CHANNEL; i++) {
                data[i].x = i;
                data[i].y = samples[i];
            }
        }

        this._chartRef.current.update();
    }
}

export default HistogramView;
