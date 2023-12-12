import { fromTo, type HRSizeType } from '@tsmx/human-readable';

const S = new Map<string, HRSizeType>();
S.set('KiB', <HRSizeType>'KBYTE');
S.set('MiB', <HRSizeType>'MBYTE');
S.set('GiB', <HRSizeType>'GBYTE');
S.set('TiB', <HRSizeType>'TBYTE');
S.set('PiB', <HRSizeType>'PBYTE');

export const FromToBytes = (num: number, prefix: string): number => {
    return Number(fromTo(num, S.get(prefix), 'BYTE', { mode: 'IEC', fullPrecision: true, numberOnly: true }));
};
