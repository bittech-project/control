import { fromBytes } from '@tsmx/human-readable';

export const BytesToReadable = (sizeInBytes: number, numberOnly = false) => {
    return fromBytes(sizeInBytes, { numberOnly: numberOnly, mode: 'IEC' });
};
