/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */


import React, { useState, useRef, useEffect, useCallback } from "react";

import classnames from "classnames";

import "./DraggablePanel.css";

interface Position {
    x: number;
    y: number;
}

interface DraggablePanelProps extends React.PropsWithChildren<NonNullable<unknown>> {
    title: string;
    className?: string;
    panelId: string; // Unique identifier for position persistence
    container?: Element | null; // Reference to container for boundaries
    defaultPosition?: Position;
    zIndex?: number;
    onActive: () => void;
}

const DraggablePanel: React.FC<DraggablePanelProps> = ({
    title,
    className,
    children,
    panelId,
    container,
    defaultPosition = { x: 100, y: 100 },
    zIndex,
    onActive,
    ...rest
}) => {
    const panelRef = useRef<HTMLDivElement>(null);
    const storageKey = `draggablePanel_${panelId}_position`;
    
    // Load saved position from localStorage
    const loadPosition = useCallback((): Position | null => {
        try {
            const saved = localStorage.getItem(storageKey);
            return saved ? JSON.parse(saved) : null;
        } catch {
            return null;
        }
    }, [storageKey]);

    // Save position to localStorage
    const savePosition = useCallback((position: Position) => {
        try {
            localStorage.setItem(storageKey, JSON.stringify(position));
        } catch {
            // Ignore storage errors
        }
    }, [storageKey]);

    // Initialize state with saved or default position
    const [position, setPosition] = useState<Position>(() => {
        const savedPosition = loadPosition();
        return savedPosition || defaultPosition;
    });
    
    const [isDragging, setIsDragging] = useState(false);
    const [dragOffset, setDragOffset] = useState<Position>({ x: 0, y: 0 });

    // Constrain position within container bounds
    const constrainPosition = useCallback((newPosition: Position): Position => {
        if (container && panelRef.current) {
            const panel = panelRef.current;

            const containerRect = container.getBoundingClientRect();
            const panelRect = panel.getBoundingClientRect();

            const maxX = containerRect.width - panelRect.width;
            const maxY = containerRect.height - panelRect.height;

            return {
                x: Math.max(0, Math.min(maxX, newPosition.x)),
                y: Math.max(0, Math.min(maxY, newPosition.y))
            };
        }
        else {
            return newPosition;
        }
    }, [container]);

    // Handle mouse move during drag
    const handleMouseMove = useCallback((event: MouseEvent) => {
        if (isDragging) {
            event.preventDefault();

            const containerRect = container?.getBoundingClientRect();
            const baseX = containerRect ? containerRect.left : 0;
            const baseY = containerRect ? containerRect.top : 0;

            // console.log(`MouseMove: Coordinates(${event.clientX}, ${event.clientY})`);
            // console.log(`MouseMove: Container Rectangle ${containerRect ? `[left:${containerRect.left}, top:${containerRect.top}, right:${containerRect.right}, bottom:${containerRect.bottom}]` : 'null'}`);
            // console.log(`MouseMove: Offests(${dragOffset.x, dragOffset.y})`);

            const newPosition = {
                x: event.clientX - dragOffset.x - baseX,
                y: event.clientY - dragOffset.y - baseY
            };

            const constrainedPosition = constrainPosition(newPosition);
            setPosition(constrainedPosition);
        }
    }, [isDragging, dragOffset, container, constrainPosition]);

    // Handle mouse up to end drag
    const handleMouseUp = useCallback(() => {
        if (isDragging) {
            setIsDragging(false);
            // Save position when drag ends
            savePosition(position);
        }
    }, [isDragging, position, savePosition]);

    // Handle mouse down to start drag
    const handleMouseDown = useCallback((event: React.MouseEvent<HTMLDivElement>) => {
        event.preventDefault();

        onActive?.();

        if (panelRef.current) {
            const panelRect = panelRef.current.getBoundingClientRect();

            console.log(`MouseDown: Coordinates(${event.clientX},${event.clientY})`);
            console.log(`MouseDown: Panel Rectangle [left:${panelRect.left}, top:${panelRect.top}, right:${panelRect.right}, bottom:${panelRect.bottom}, width:${panelRect.width}, height:${panelRect.height}]`);

            setIsDragging(true);
            setDragOffset({
                x: event.clientX - panelRect.left,
                y: event.clientY - panelRect.top
            });
        }
    }, [container]);

    // Set up global mouse event listeners
    useEffect(() => {
        if (isDragging) {
            document.addEventListener('mousemove', handleMouseMove);
            document.addEventListener('mouseup', handleMouseUp);
            
            return () => {
                document.removeEventListener('mousemove', handleMouseMove);
                document.removeEventListener('mouseup', handleMouseUp);
            };
        }
    }, [isDragging, handleMouseMove, handleMouseUp]);

    // Component style
    const style: React.CSSProperties = {
        position: "absolute",
        left: `${position.x}px`,
        top: `${position.y}px`,
        zIndex: zIndex
    };

    return (
        <div
            ref={panelRef}
            style={style}
            {...rest}
        >
            <div className={classnames("okPanel", className)}>
                <div
                    className="okPanelHeader"
                    onMouseDown={handleMouseDown}
                    style={{
                        cursor: isDragging ? 'grabbing' : 'grab',
                        userSelect: 'none',
                    }}
                >
                    <span className="okPanelTitleText">{title}</span>
                </div>
                <div className="okPanelContent">{children}</div>
            </div>
        </div>
    );
};

export default DraggablePanel;
