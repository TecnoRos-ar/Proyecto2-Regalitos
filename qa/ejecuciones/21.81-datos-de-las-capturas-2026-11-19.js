if(!sessionStorage.getItem('semilla')){
  sessionStorage.setItem('semilla','1');
  localStorage.setItem('regalitos-argentinos-v1', JSON.stringify({materiales:[
    {id:'m1',nombre:'Hilo encerado',unidad:'m',stock:50,precioCompra:3500,cantCompra:50},
    {id:'m2',nombre:'Mostacillas',unidad:'un',stock:200,precioCompra:2000,cantCompra:500},
    {id:'m3',nombre:'Cierre',unidad:'un',stock:10,precioCompra:1500,cantCompra:10}],
   productos:[{id:'p1',nombre:'Clásica',receta:[{matId:'m1',cant:0.5},{matId:'m2',cant:20},{matId:'m3',cant:1}],manoObra:100,otros:35,ganTipo:'pct',ganValor:100}],
   config:{redondeo:100,margenDefault:100},ejemplo:false}));
  location.reload();
}
