// What a port offers a device for a given voltage cap, current limit and
// cable, following the blade's table (firmware/charger-module/src/pdo.c):
// fixed 5, 9, 12, 15 and 20 V plus PPS 3.3-11 V and 3.3-21 V. The cap drops
// every option above it except 5 V, and a 20 V cap keeps the 21 V PPS range.
// Each option's current is the lowest of the current limit, 5 A, the cable
// and 100 W at the top of its range.

export const CAPS = [5, 9, 12, 15, 20];

const OPTIONS = [
	{ label: '5 V', top: 5, pps: false },
	{ label: '9 V', top: 9, pps: false },
	{ label: '12 V', top: 12, pps: false },
	{ label: '15 V', top: 15, pps: false },
	{ label: '20 V', top: 20, pps: false },
	{ label: 'PPS 3.3–11 V', top: 11, pps: true },
	{ label: 'PPS 3.3–21 V', top: 21, pps: true },
];

export type Offer = {
	label: string;
	offered: boolean;
	amps: number;
	watts: number;
	// What set the current: the port's limit, the cable or the 100 W ceiling.
	by: 'limit' | 'cable' | '100 W' | null;
};

export function offers(capV: number, limitMa: number, cableMa: number): Offer[] {
	const capMv = capV >= 20 ? 21000 : capV * 1000;
	return OPTIONS.map((o, i) => {
		const offered = (i === 0 && !o.pps) || o.top * 1000 <= capMv;
		const step = o.pps ? 50 : 10;
		const powerMa = Math.floor(100000000 / (o.top * 1000));
		let ma = Math.min(limitMa, 5000);
		let by: Offer['by'] = 'limit';
		if (cableMa < ma) [ma, by] = [cableMa, 'cable'];
		if (powerMa < ma) [ma, by] = [powerMa, '100 W'];
		ma -= ma % step;
		return {
			label: o.label,
			offered,
			amps: ma / 1000,
			watts: (o.top * ma) / 1000,
			by: offered ? by : null,
		};
	});
}
