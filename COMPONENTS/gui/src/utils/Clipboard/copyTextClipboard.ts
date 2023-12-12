export async function copyClipboard() { 
    let copyText = null;
        copyText = document.addEventListener("click", function(e){
            if (e.target.className == 'copy-text'){
                navigator.clipboard.writeText(e.target.innerHTML)
            }
        })   
}
