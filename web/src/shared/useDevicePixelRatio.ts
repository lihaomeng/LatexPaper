import { useEffect, useState } from 'react';

function readRatio(): number {
  const ratio = window.devicePixelRatio;
  return Number.isFinite(ratio) && ratio > 0 ? ratio : 1;
}

export function useDevicePixelRatio(): number {
  const [ratio, setRatio] = useState(readRatio);
  useEffect(() => {
    let query: MediaQueryList | undefined;
    const update = () => {
      query?.removeEventListener('change', update);
      const next = readRatio();
      setRatio(next);
      query = window.matchMedia(`(resolution: ${next}dppx)`);
      query.addEventListener('change', update);
    };
    update();
    window.addEventListener('resize', update);
    return () => {
      query?.removeEventListener('change', update);
      window.removeEventListener('resize', update);
    };
  }, []);
  return ratio;
}
