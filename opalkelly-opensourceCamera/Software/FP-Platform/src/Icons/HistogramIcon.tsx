/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import * as React from "react";

export interface HistogramIconProps extends React.SVGAttributes<SVGElement> {
    children?: never;
    color?: string;
}

export const HistogramIcon = React.forwardRef<SVGSVGElement, HistogramIconProps>(
    function render(
        { color = "currentColor", ...props },
        forwardedRef,
    ) {
        return (
            <svg
                xmlns="http://www.w3.org/2000/svg"
                width="18"
                height="18"
                viewBox="0 0 18 18"
                fill="none"
                ref={forwardedRef}
                {...props}
            >
                <path fill-rule="evenodd" clip-rule="evenodd" d="M1 0.5C1.27614 0.5 1.5 0.723858 1.5 1V16.5H17C17.2761 16.5 17.5 16.7239 17.5 17C17.5 17.2761 17.2761 17.5 17 17.5H1C0.723858 17.5 0.5 17.2761 0.5 17V1C0.5 0.723858 0.723858 0.5 1 0.5Z" fill={color}/>
                <path fill-rule="evenodd" clip-rule="evenodd" d="M2.32812 15.4813C2.06884 15.3863 1.93565 15.0991 2.03064 14.8398L5.49874 5.55805C5.76662 4.82688 6.79461 4.81006 7.08627 5.53206L9.09353 10.5009L10.7279 8.39539C11.1056 7.90879 11.8592 7.97248 12.1499 8.51558L14.2845 12.5037L14.8069 11.9483C15.2506 11.4767 16.0376 11.654 16.2362 12.2703L17.1034 14.8203C17.2137 15.1447 16.9768 15.4813 16.6343 15.4813C13.5064 15.4813 3.6196 15.4813 2.32812 15.4813Z" fill={color}/>
            </svg>
        );
    },
);

export default HistogramIcon;
