/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import * as React from "react";

export interface ObjectDetectionIconProps extends React.SVGAttributes<SVGElement> {
    children?: never;
    color?: string;
}

export const ObjectDetectionIcon = React.forwardRef<SVGSVGElement, ObjectDetectionIconProps>(
    function render(
        { color = "currentColor", ...props },
        forwardedRef,
    ) {
        return (
            <svg
                xmlns="http://www.w3.org/2000/svg"
                width="24"
                height="24"
                viewBox="0 0 24 24"
                fill="none"
                ref={forwardedRef}
                {...props}
            >
                <path d="M1 8V2C1 1.44772 1.44772 1 2 1H7.5" stroke="white" stroke-linecap="round"/>
                <path d="M23 16L23 22C23 22.5523 22.5523 23 22 23L16.5 23" stroke="white" stroke-linecap="round"/>
                <path d="M16 1L22 1C22.5523 1 23 1.44772 23 2L23 7.5" stroke="white" stroke-linecap="round"/>
                <path d="M8 23L2 23C1.44772 23 1 22.5523 1 22L1 16.5" stroke="white" stroke-linecap="round"/>
                <path d="M6.5 9L12 6L17.5 9L12 12L6.5 9Z" stroke="white" stroke-linecap="round" stroke-linejoin="round"/>
                <path d="M6.5 9V15L12 18L17.5 15V9" stroke="white" stroke-linecap="round" stroke-linejoin="round"/>
                <path d="M19.312 17.652H21.3783V18.1312H20.6294V20.6816H21.3783V21.1585H19.312V20.6816H20.0441V18.1312H19.312V17.652Z" fill="white"/>
                <path d="M17.9411 20.3517H16.8405L16.6021 21.1585H16L17.1415 17.652H17.6545L18.7768 21.1585H18.1771L17.9411 20.3517ZM16.9874 19.8556H17.799L17.3968 18.4684L16.9874 19.8556Z" fill="white"/>
            </svg>
        );
    },
);

export default ObjectDetectionIcon;
