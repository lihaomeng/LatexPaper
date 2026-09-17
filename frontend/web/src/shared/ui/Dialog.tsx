import { useEffect, useRef, type ReactNode } from "react";

/** Native modal semantics provide focus containment, inert background and focus restoration. */
export function Dialog({ children, onClose, className = "", labelId }: {
  children: ReactNode; onClose(): void; className?: string; labelId: string;
}) {
  const dialog = useRef<HTMLDialogElement>(null);
  useEffect(() => {
    const element = dialog.current;
    const previous = document.activeElement;
    element?.showModal();
    return () => {
      element?.close();
      if (previous instanceof HTMLElement && previous.isConnected) previous.focus();
    };
  }, []);
  return <dialog ref={dialog} className={"modal " + className} aria-labelledby={labelId}
    onCancel={event => { event.preventDefault(); onClose(); }}>
    {children}
  </dialog>;
}
