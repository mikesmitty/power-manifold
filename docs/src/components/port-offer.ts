// What a port offers a device for a given voltage cap, power limit and
// cable, following the blade's table (firmware/charger-module/src/pdo.c):
// fixed 5, 9, 12, 15 and 20 V, SPR AVS from 9 V and one PPS range. The cap
// drops every fixed option above it except 5 V. SPR AVS needs 15 V and stops
// at 15 V under a 15 V cap. PPS is 5-21 V with no cap (a 20 V cap keeps it)
// and 5-11 V under a 12 V or 15 V cap. Each fixed option's current is the
// lowest of the power limit at its voltage, 5 A and the cable.

export const CAPS = [5, 9, 12, 15, 20];

export type Offer = {
	label: string;
	offered: boolean;
	amps: number; // at the option's top voltage
	watts: number;
	// What set the current: the port's power limit, the cable or the 5 A maximum.
	by: 'limit' | 'cable' | '5 A' | null;
};

const FIXED = [5, 9, 12, 15, 20];

// The current an option at v volts carries, rounded down to step mA, and
// what set it
function current(v: number, limitW: number, cableMa: number, step: number): [number, Offer['by']] {
	let ma = Math.floor((limitW * 1000000) / (v * 1000));
	let by: Offer['by'] = 'limit';
	if (5000 < ma) [ma, by] = [5000, '5 A'];
	if (cableMa < ma) [ma, by] = [cableMa, 'cable'];
	return [ma - (ma % step), by];
}

export function offers(capV: number, limitW: number, cableMa: number): Offer[] {
	const capMv = capV >= 20 ? 21000 : capV * 1000;
	const rows: Offer[] = FIXED.map((v, i) => {
		const offered = i === 0 || v * 1000 <= capMv;
		const [ma, by] = current(v, limitW, cableMa, 10);
		return { label: `${v} V`, offered, amps: ma / 1000, watts: (v * ma) / 1000, by: offered ? by : null };
	});

	// SPR AVS, shown at its top voltage: the 20 V option's current, or the
	// 15 V one's when the range stops there
	const avsTop = capMv >= 20000 ? 20 : 15;
	const avsOffered = capMv >= 15000;
	const [avsMa, avsBy] = current(avsTop, limitW, cableMa, 10);
	rows.push({
		label: `SPR AVS 9–${avsTop} V`,
		offered: avsOffered,
		amps: avsMa / 1000,
		watts: (avsTop * avsMa) / 1000,
		by: avsOffered ? avsBy : null,
	});

	// PPS, shown at its top voltage. Its current is figured at 20 V (or 9 V
	// for the 11 V range), and the port holds a device to the power limit.
	const wide = capMv >= 21000;
	const ppsTop = wide ? 21 : 11;
	const ppsOffered = capMv >= 11000;
	const [ppsMa, ppsBy] = current(wide ? 20 : 9, limitW, cableMa, 50);
	rows.push({
		label: `PPS 5–${ppsTop} V`,
		offered: ppsOffered,
		amps: Math.min(ppsMa, Math.floor((limitW * 1000000) / (ppsTop * 1000))) / 1000,
		watts: Math.min((ppsTop * ppsMa) / 1000, limitW),
		by: ppsOffered ? ppsBy : null,
	});
	return rows;
}
