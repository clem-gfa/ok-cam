/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import * as React from "react";

export interface CaptureStatusIconProps extends React.SVGAttributes<SVGElement> {
    children?: never;
    color?: string;
}

export const CaptureStatusIcon = React.forwardRef<SVGSVGElement, CaptureStatusIconProps>(
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
                <path d="M9 0C13.9706 0 18 4.02944 18 9C18 13.9706 13.9706 18 9 18C4.02944 18 0 13.9706 0 9C0 4.02944 4.02944 0 9 0ZM9 1C4.58172 1 1 4.58172 1 9C1 13.4183 4.58172 17 9 17C13.4183 17 17 13.4183 17 9C17 4.58172 13.4183 1 9 1ZM9 8C9.27614 8 9.5 8.22386 9.5 8.5V12C9.5 12.2761 9.27614 12.5 9 12.5C8.72386 12.5 8.5 12.2761 8.5 12V8.5C8.5 8.22386 8.72386 8 9 8ZM9 5.5C9.41421 5.5 9.75 5.83579 9.75 6.25C9.75 6.66421 9.41421 7 9 7C8.58579 7 8.25 6.66421 8.25 6.25C8.25 5.83579 8.58579 5.5 9 5.5Z" fill="white"/>
            </svg>
        );
    },
);

export default CaptureStatusIcon;
