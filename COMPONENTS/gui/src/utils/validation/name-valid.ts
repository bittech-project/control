export const NameValid = (name: string) => {
    return /^[\w\dа-яА-ЯёЁ_.-]{0,128}$/gs.test(name);
};
