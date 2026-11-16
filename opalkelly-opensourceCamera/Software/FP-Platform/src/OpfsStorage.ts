/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Recording storage for the FrontPanel app host. The host refuses showSaveFilePicker /
// showDirectoryPicker, but allows the Origin Private File System (OPFS, fast, ~6 GB quota) and lets a page
// hand a File from it to the user with <a download>. So recordings are written to the OPFS in segments and
// each closed segment is exported, then removed (a few are kept in case the download is still running).

import { AudioFileSink, AudioSinkFactory } from "./AudioStream";

type Writable = {
    write: (data: Uint8Array<ArrayBuffer>) => Promise<void>;
    seek: (position: number) => Promise<void>;
    close: () => Promise<void>;
};

async function opfsRoot(): Promise<FileSystemDirectoryHandle> {
    const storage = navigator.storage as StorageManager | undefined;
    if (!storage || typeof storage.getDirectory !== "function") throw new Error("OPFS is not available");
    return storage.getDirectory();
}

export const opfsSinkFactory: AudioSinkFactory = async (fileName: string): Promise<AudioFileSink> => {
    const root = await opfsRoot();
    const handle = await root.getFileHandle(fileName, { create: true });
    const writable = (await handle.createWritable()) as unknown as Writable;
    const own = (d: Uint8Array): Uint8Array<ArrayBuffer> =>
        d.buffer instanceof ArrayBuffer ? (d as Uint8Array<ArrayBuffer>) : new Uint8Array(d);
    return {
        write: (data) => writable.write(own(data)),
        overwrite: async (offset, data) => {
            await writable.seek(offset);
            await writable.write(own(data));
        },
        close: () => writable.close(),
    };
};

/** Hands a file of the OPFS to the user (a download) and lets old exported files be removed. */
export class OpfsExporter {
    private readonly _exported: string[] = [];
    private readonly _keep: number;

    constructor(keepExportedFiles = 4) {
        this._keep = keepExportedFiles;
    }

    async exportFile(fileName: string): Promise<void> {
        const root = await opfsRoot();
        const handle = await root.getFileHandle(fileName);
        const file = await handle.getFile(); // disk-backed: not loaded in memory
        const url = URL.createObjectURL(file);
        const a = document.createElement("a");
        a.href = url;
        a.download = fileName;
        document.body.appendChild(a);
        a.click();
        a.remove();
        setTimeout(() => URL.revokeObjectURL(url), 10 * 60 * 1000);
        this._exported.push(fileName);
        while (this._exported.length > this._keep) {
            const old = this._exported.shift()!;
            await root.removeEntry(old).catch(() => undefined);
        }
    }
}
